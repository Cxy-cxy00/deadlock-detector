/* report.c — พิมพ์รายงานและ export Graphviz  (ผู้รับผิดชอบ: คนที่ 5)
 *
 * ฟังก์ชันในไฟล์นี้ถูกเรียกจาก detector thread ซึ่งไม่ได้อยู่ใน lock path ของ
 * โปรแกรมเป้าหมาย จึงใช้ buffer บน stack, snprintf และแม้แต่ popen ได้
 * แต่ยังต้องออกทาง dd_logf (write(2,...) ตรง ๆ) ไม่ใช่ printf
 * เพราะ stdio ของโปรแกรมเป้าหมายอาจค้างคาอยู่ตอนที่เรากำลังจะรายงาน
 */
#include <dlfcn.h>    /* dladdr */
#include <stdio.h>    /* snprintf, popen */
#include <stdlib.h>   /* atoi */
#include "report.h"
#include "state.h"

/* ---- ที่เก็บชื่อไฟล์ที่ resolve ได้ ----
 * dd_site_t.file เป็น const char* จึงต้องมีที่เก็บที่อยู่ได้นานกว่าตัวฟังก์ชัน
 * ใช้ pool วนใช้ซ้ำแทน malloc  ปลอดภัยเพราะ report พิมพ์ทันทีหลัง resolve
 * และมีแต่ detector thread คนเดียวที่แตะตรงนี้ จึงไม่ต้องล็อก
 */
#define DD_NAME_SLOTS 8
#define DD_NAME_LEN   128
static char g_names[DD_NAME_SLOTS][DD_NAME_LEN];
static int  g_name_next = 0;

/* ---- หา site ที่บันทึกไว้ตอนล็อก (ต้องถือ dd_state_lock อยู่) ---- */

static const dd_site_t *site_of_wait(dd_tid_t t) {
    const dd_wait_t *w;
    size_t n = dd_state_waits(&w);
    for (size_t i = 0; i < n; i++)
        if (w[i].waiter == t) return &w[i].site;
    return NULL;
}

static const dd_site_t *site_of_hold(dd_mutex_t m) {
    const dd_hold_t *h;
    size_t n = dd_state_holds(&h);
    for (size_t i = 0; i < n; i++)
        if (h[i].mutex == m) return &h[i].site;
    return NULL;
}

/* เราเอา path ไปประกอบเป็นคำสั่ง shell ให้ popen จึงต้องกันอักขระที่ทำให้ความหมายเพี้ยน
 * เขียนเป็นรหัส ASCII เพื่อเลี่ยงความสับสนเรื่องการ escape ตัว backslash เอง
 */
static int path_is_shell_safe(const char *p) {
    for (; *p != 0; p++) {
        int ch = (unsigned char)*p;
        if (ch == 39 ||    /* ตัวอัญประกาศเดี่ยว */
            ch == 34 ||    /* ตัวอัญประกาศคู่    */
            ch == 96 ||    /* backtick           */
            ch == 36 ||    /* dollar             */
            ch == 92 ||    /* backslash          */
            ch == 10)      /* ขึ้นบรรทัดใหม่      */
            return 0;
    }
    return 1;
}

/* ---- แปลง pc ดิบ -> file:line ----
 *
 * ทำไมเอา pc ยัดใส่ addr2line ตรง ๆ ไม่ได้: pc เป็น "ที่อยู่ตอนรัน" แต่ addr2line
 * ต้องการ "ที่อยู่ในไฟล์"  โปรแกรมสมัยใหม่ compile เป็น PIE และระบบมี ASLR
 * ทำให้ module ถูกโหลดที่ตำแหน่งสุ่มใหม่ทุกครั้งที่รัน
 *
 * dladdr() บอก base address ที่ module นั้นถูกโหลด (dli_fbase) และชื่อไฟล์ (dli_fname)
 * ลบ base ออกก็ได้ offset ในไฟล์  (ไฟล์ non-PIE จะได้ dli_fbase = 0 สูตรเดียวกันก็ยังถูก)
 *
 * ข้อควรระวังที่ยอมรับไว้: popen = fork + exec  การ fork จากโปรเซสที่มีหลาย thread
 * มีความเสี่ยงถ้าบังเอิญมี thread อื่นถือ lock ภายในของ libc อยู่พอดีตอน fork
 * ในที่นี้ความเสี่ยงต่ำเพราะ thread ที่ค้างอยู่ค้างใน futex ของ mutex ไม่ได้อยู่ใน malloc
 * และเรา resolve แค่ตอนรายงาน (ครั้งเดียวต่อหนึ่ง cycle) ไม่ใช่ทุกครั้งที่ล็อก
 */
void dd_report_resolve_site(dd_site_t *site) {
    Dl_info       info;
    char          cmd[512];
    char          out[512];
    FILE         *fp;
    char         *colon;
    const char   *base;
    unsigned long off;
    int           line;

    if (site == NULL || site->pc == NULL || site->file != NULL)
        return;                                  /* ไม่มีอะไรให้ทำ หรือ resolve ไปแล้ว */

    if (dladdr(site->pc, &info) == 0 || info.dli_fname == NULL)
        return;
    if (!path_is_shell_safe(info.dli_fname)) {
        dd_dbgf("resolve: ข้าม path ที่มีอักขระพิเศษ: %s\n", info.dli_fname);
        return;
    }

    off = (unsigned long)((const char *)site->pc - (const char *)info.dli_fbase);

    /* __builtin_return_address คืน "ที่อยู่ของคำสั่งถัดจาก call" ไม่ใช่ตัว call เอง
     * ถ้าไม่ลบ 1 addr2line จะชี้บรรทัดถัดไปแทนบรรทัดที่เรียก lock จริง ๆ
     */
    if (off > 0) off--;

    snprintf(cmd, sizeof cmd, "addr2line -e '%s' 0x%lx 2>/dev/null",
             info.dli_fname, off);

    fp = popen(cmd, "r");
    if (fp == NULL) return;
    if (fgets(out, sizeof out, fp) == NULL) { pclose(fp); return; }
    pclose(fp);

    for (char *p = out; *p != '\0'; p++)
        if (*p == '\n') { *p = '\0'; break; }

    /* addr2line คืน "path/file.c:123" หรือ "??:0" ถ้าหาไม่เจอ — เอาทวิภาคตัวท้ายสุด */
    colon = NULL;
    for (char *p = out; *p != '\0'; p++)
        if (*p == ':') colon = p;
    if (colon == NULL) return;
    *colon = '\0';
    line = atoi(colon + 1);

    if (out[0] == '?' || line <= 0) {
        dd_dbgf("resolve: addr2line หาไม่เจอ — โปรแกรมเป้าหมาย compile ด้วย -g หรือยัง\n");
        return;
    }

    /* เอาเฉพาะชื่อไฟล์ ไม่เอา path ยาว ๆ ให้รายงานอ่านง่ายเหมือนตัวอย่างใน README */
    base = out;
    for (const char *p = out; *p != '\0'; p++)
        if (*p == '/') base = p + 1;

    /* ใช้ %.*s จำกัดความยาวให้ชัดเจน ชื่อไฟล์ยาวเกินก็ตัด
     * (snprintf ตัดให้อยู่แล้ว แต่เขียนแบบนี้ gcc ถึงจะเลิกเตือน -Wformat-truncation) */
    snprintf(g_names[g_name_next], DD_NAME_LEN, "%.*s", DD_NAME_LEN - 1, base);
    site->file  = g_names[g_name_next];
    site->line  = line;
    g_name_next = (g_name_next + 1) % DD_NAME_SLOTS;
}

static void print_line(const char *label, dd_mutex_t m, dd_site_t *s) {
    dd_report_resolve_site(s);
    if (s->file != NULL)
        dd_logf("      %-7s mutex %p   at %s:%d\n", label, m, s->file, s->line);
    else if (s->pc != NULL)
        dd_logf("      %-7s mutex %p   at pc %p\n", label, m, s->pc);
    else
        dd_logf("      %-7s mutex %p\n", label, m);
}

void dd_report_cycle(const dd_cycle_t *c) {
    struct {
        dd_mutex_t held;
        dd_mutex_t waiting;
        dd_site_t  hold_site;
        dd_site_t  wait_site;
    } row[DD_MAX_CYCLE_LEN];
    char   buf[1024];
    int    off;
    size_t i;

    if (c == NULL || c->len == 0) return;

    /* ดึงข้อมูลที่ต้องใช้ออกมาให้ครบก่อนใต้ล็อก แล้วปล่อยล็อกทันที
     * เพราะขั้นตอนพิมพ์ต้อง fork addr2line ซึ่งกินเวลาเป็นมิลลิวินาที
     * ไม่ควรถือล็อกตารางค้างไว้ระหว่างนั้น
     */
    dd_state_lock();
    for (i = 0; i < c->len; i++) {
        const dd_site_t *s;
        const dd_site_t  empty = { NULL, 0, NULL };

        /* tids[i] รอ mutexes[i] ซึ่งถือโดย tids[i+1]
         * ดังนั้น tids[i] ย่อมถือ mutexes[i-1] คือตัวที่คนก่อนหน้าในวงกำลังรออยู่
         */
        row[i].waiting = c->mutexes[i];
        row[i].held    = c->mutexes[(i + c->len - 1) % c->len];

        s = site_of_hold(row[i].held);
        row[i].hold_site = (s != NULL) ? *s : empty;
        s = site_of_wait(c->tids[i]);
        row[i].wait_site = (s != NULL) ? *s : empty;
    }
    dd_state_unlock();

    dd_logf("\n[DEADLOCK DETECTED] %zu threads in cycle\n\n", c->len);
    for (i = 0; i < c->len; i++) {
        dd_logf("  Thread %lu\n", c->tids[i]);
        print_line("holds",   row[i].held,    &row[i].hold_site);
        print_line("waiting", row[i].waiting, &row[i].wait_site);
    }

    /* ประกอบบรรทัด Cycle ในบัฟเฟอร์เดียวแล้วพิมพ์ทีเดียว จะได้ไม่ถูกแทรกกลาง */
    off = snprintf(buf, sizeof buf, "\n  Cycle: ");
    for (i = 0; i < c->len && off > 0 && off < (int)sizeof buf; i++)
        off += snprintf(buf + off, sizeof buf - (size_t)off, "T%lu -> ", c->tids[i]);
    if (off > 0 && off < (int)sizeof buf)
        snprintf(buf + off, sizeof buf - (size_t)off, "T%lu\n", c->tids[0]);
    dd_logf("%s", buf);

    dd_logf("  Hint : acquire locks in a consistent global order\n\n");
}

void dd_report_dot(const dd_cycle_t *c, const char *path) {
    (void)c; (void)path;
    /* TODO ขั้น 10: เขียน digraph ออกไฟล์ .dot
     * path == NULL -> อ่านจาก env DD_DOT_OUT  ถ้าไม่ได้ตั้งไว้ก็ไม่ต้องทำอะไร
     */
}
