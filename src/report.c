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
#include <string.h>   /* strlen */
#include <stdarg.h>   /* va_list ของ dot_emit */
#include <fcntl.h>    /* open */
#include <unistd.h>   /* write, close */
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
 * ลบ base ออกก็ได้ offset ในไฟล์ — ถูกเฉพาะ PIE / shared object
 * binary แบบ non-PIE มี dli_fbase เป็นที่อยู่โหลดจริง (เช่น 0x400000) offset จึงผิด
 * addr2line หาไม่เจอ แล้ว print_line ถอยไปแสดง "at pc 0x..." แทน (ข้อจำกัดใน README)
 *
 * ข้อควรระวังที่ยอมรับไว้: glibc รุ่นใหม่ทำ popen ด้วย posix_spawn (child exec ทันที)
 * จึงไม่มีปัญหา fork แล้ว child ไปเจอ lock ภายในที่ค้างอยู่  แต่ตัว popen เองยังเรียก
 * malloc และ lock ภายในของ stdio  ความเสี่ยงต่ำเพราะ thread ที่ค้างอยู่ค้างใน futex
 * ของ mutex ไม่ได้อยู่ใน malloc และเรา resolve แค่ตอนรายงาน (ครั้งเดียวต่อหนึ่ง cycle)
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

        /* partial: คนก่อนหน้า tids[0] ในวงถูกตัดทิ้งไป จึงไม่รู้ว่า tids[0] ถือตัวไหน
         * (mutexes[len-1] คือตัวที่ tids[len-1] รอ ซึ่งถือโดย thread ที่ถูกตัด ไม่ใช่ tids[0])
         */
        if (c->partial && i == 0) {
            row[i].held      = NULL;
            row[i].hold_site = empty;
        } else {
            row[i].held = c->mutexes[(i + c->len - 1) % c->len];
            s = site_of_hold(row[i].held);
            row[i].hold_site = (s != NULL) ? *s : empty;
        }
        s = site_of_wait(c->tids[i]);
        row[i].wait_site = (s != NULL) ? *s : empty;
    }
    dd_state_unlock();

    if (c->partial)
        dd_logf("\n[DEADLOCK DETECTED] %zu threads in cycle "
                "(partial path, truncated: แสดง %zu)\n\n", c->full_len, c->len);
    else
        dd_logf("\n[DEADLOCK DETECTED] %zu threads in cycle\n\n", c->len);
    for (i = 0; i < c->len; i++) {
        dd_logf("  Thread %lu\n", c->tids[i]);
        if (row[i].held != NULL)
            print_line("holds", row[i].held, &row[i].hold_site);
        else
            dd_logf("      holds   (ไม่ทราบ — อยู่นอกช่วงที่ถูกตัด)\n");
        print_line("waiting", row[i].waiting, &row[i].wait_site);
    }

    /* ประกอบบรรทัด Cycle ในบัฟเฟอร์เดียวแล้วพิมพ์ทีเดียว จะได้ไม่ถูกแทรกกลาง
     * partial: จบด้วย "..." แทนการวนกลับ T0 — ห้ามปิดวงที่ไม่มีจริง
     */
    off = snprintf(buf, sizeof buf, "\n  Cycle: ");
    for (i = 0; i < c->len && off > 0 && off < (int)sizeof buf; i++)
        off += snprintf(buf + off, sizeof buf - (size_t)off, "T%lu -> ", c->tids[i]);
    if (off > 0 && off < (int)sizeof buf) {
        if (c->partial)
            snprintf(buf + off, sizeof buf - (size_t)off,
                     "... (partial path, truncated)\n");
        else
            snprintf(buf + off, sizeof buf - (size_t)off, "T%lu\n", c->tids[0]);
    }
    /* วงยาว ๆ ทำให้บรรทัดนี้ยาวเกิน buffer 512 ของ dd_logf (log.c) แล้วถูกตัดท้ายทิ้ง
     * จึงทยอยพิมพ์ทีละช่วง  ตอนนี้ thread ของเป้าหมายค้างกันหมดแล้ว จึงไม่มีใครมาแทรกกลาง
     */
    for (size_t pos = 0, n = strlen(buf); pos < n; pos += 400)
        dd_logf("%.*s", (int)(n - pos < 400 ? n - pos : 400), buf + pos);

    dd_logf("  Hint : acquire locks in a consistent global order\n\n");
}

/* ---- คำเตือน lock-order inversion ----
 * ใช้คำว่า locked / then ไม่ใช้ holds / waiting และไม่มีบรรทัด "Thread <tid>" เดี่ยว ๆ
 * เพราะ tools/playground.py อ่านบรรทัดรูปแบบนั้นเป็นส่วนของรายงาน deadlock
 * และไม่มีคำว่า WARNING เพราะ tests/run_regress.sh นับคำนั้นเป็นคำเตือนเรื่องตาราง
 */
void dd_report_inversion(const dd_inversion_t *v) {
    dd_order_t o[2];

    if (v == NULL) return;
    o[0] = v->seen;       /* copy ออกมาก่อน เพราะ print_line เขียนชื่อไฟล์ลงใน site */
    o[1] = v->reverse;

    dd_logf("\n[LOCK ORDER INVERSION] 2 mutexes locked in opposite orders\n\n");
    for (int k = 0; k < 2; k++) {
        dd_logf("  Order %d (thread %lu): mutex %p -> mutex %p\n",
                k + 1, o[k].tid, o[k].first, o[k].second);
        print_line("locked", o[k].first, &o[k].first_site);
        print_line("then", o[k].second, &o[k].second_site);
    }
    dd_logf("\n  Risk : no deadlock yet — if both orders run at the same time, they will deadlock\n");
    dd_logf("  Hint : acquire locks in a consistent global order\n\n");
}


/* ---- export เป็น Graphviz ----
 * วาดแบบ resource allocation graph ตามตำรา:
 *   วงรี = thread,  สี่เหลี่ยม = mutex
 *   T -> M  คือ "กำลังขอ"        (request edge)
 *   M -> T  คือ "ถูกถืออยู่โดย"  (assignment edge)
 * ลูกศรจะวนครบรอบพอดี ได้ภาพ deadlock แบบเดียวกับในสไลด์เรียน
 *
 * ใช้ open/write แทน fopen/fprintf เพื่อไม่ให้มี malloc เข้ามาเกี่ยวเลย
 * (fopen เรียก malloc ซึ่งเสี่ยงถ้าเป้าหมายค้างคา lock ของ malloc อยู่พอดี)
 */

/* ตัวอัญประกาศคู่ เขียนเป็นรหัส ASCII แล้วยัดผ่าน %c
 * จะได้ไม่ต้องมี backslash เกลื่อน format string จนอ่านไม่ออก
 */
#define DQ 34

/* หนึ่งครั้งที่เรียก = หนึ่งบรรทัดในไฟล์ (ต่อท้ายขึ้นบรรทัดใหม่ให้เอง) */
static void dot_emit(int fd, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void dot_emit(int fd, const char *fmt, ...) {
    char    buf[512];
    va_list ap;
    int     n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof buf - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof buf - 2) n = (int)sizeof buf - 2;
    buf[n] = 10;
    if (write(fd, buf, (size_t)n + 1) < 0) return;   /* เขียนไม่ได้ก็ยอม */
}

/* บรรทัดว่างคั่น — แยกออกมาเพราะ dot_emit(fd, "") จะโดน -Wformat-zero-length */
static void dot_blank(int fd) {
    const char nl = 10;
    if (write(fd, &nl, 1) < 0) return;
}

/* ข้อความหัวของหนึ่งวง ใช้ทั้งเป็น label ของทั้งกราฟ (วงเดียว) และของ cluster (หลายวง) */
static void cycle_title(const dd_cycle_t *c, char *buf, size_t size) {
    if (c->partial)
        snprintf(buf, size, "DEADLOCK: %zu threads in cycle (partial path, truncated: แสดง %zu)",
                 c->full_len, c->len);
    else
        snprintf(buf, size, "DEADLOCK: %zu threads in cycle", c->len);
}

/* วาดหนึ่งวง  pfx คือคำนำหน้าชื่อ node ("" ถ้ามีวงเดียว, "C1_" ฯลฯ ถ้าหลายวง)
 * ชื่อ node ต้องไม่ซ้ำข้ามวง ไม่งั้น Graphviz จะรวมเป็น node เดียวกัน
 */
static void dot_cycle(int fd, const dd_cycle_t *c, const char *pfx, const char *ind) {
    size_t i;

    for (i = 0; i < c->len; i++) {
        dot_emit(fd, "%s%sT%zu [shape=ellipse, style=filled, fillcolor=mistyrose,"
                     " label=%cThread %lu%c];", ind, pfx, i, DQ, c->tids[i], DQ);
        dot_emit(fd, "%s%sM%zu [shape=box, style=filled, fillcolor=lightblue,"
                     " label=%cmutex %p%c];", ind, pfx, i, DQ, c->mutexes[i], DQ);
    }
    dot_blank(fd);

    for (i = 0; i < c->len; i++) {
        /* tids[i] กำลังขอ mutexes[i] */
        dot_emit(fd, "%s%sT%zu -> %sM%zu [color=red, label=waiting];", ind, pfx, i, pfx, i);
        /* partial: mutex ตัวสุดท้ายถือโดย thread ที่ถูกตัดทิ้ง ไม่ใช่ T0
         * ชี้ไปที่ node "..." แทน ห้ามวาดเส้นปิดวงปลอม
         */
        if (c->partial && i + 1 == c->len) {
            dot_emit(fd, "%s%smore [shape=plaintext, label=%c... (partial path, truncated)%c];",
                     ind, pfx, DQ, DQ);
            dot_emit(fd, "%s%sM%zu -> %smore [style=dashed, label=%cheld by%c];",
                     ind, pfx, i, pfx, DQ, DQ);
            continue;
        }
        /* mutexes[i] ถือโดย tids[i+1] — เส้นสุดท้ายคือตัวปิดวงให้ครบรอบ */
        dot_emit(fd, "%s%sM%zu -> %sT%zu [label=%cheld by%c];",
                 ind, pfx, i, pfx, (i + 1) % c->len, DQ, DQ);
    }
}

void dd_report_dot(const dd_cycle_t *cs, size_t n, const char *path) {
    int    fd;
    char   title[160];
    char   pfx[24];   /* "C" + size_t สูงสุด 20 หลัก + "_" + NUL */

    if (cs == NULL || n == 0) return;
    if (path == NULL) path = getenv("DD_DOT_OUT");
    if (path == NULL || *path == 0) return;    /* ไม่ได้สั่งให้เขียน ก็ไม่ต้องทำ */

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        dd_logf("[dd] WARNING: เขียนไฟล์ %s ไม่ได้\n", path);
        return;
    }

    dot_emit(fd, "digraph deadlock {");
    dot_emit(fd, "  rankdir=LR;");
    dot_emit(fd, "  labelloc=t;");
    if (n == 1) {
        cycle_title(&cs[0], title, sizeof title);
        dot_emit(fd, "  label=%c%s%c;", DQ, title, DQ);
    } else {
        dot_emit(fd, "  label=%cDEADLOCK: %zu cycles%c;", DQ, n, DQ);
    }
    dot_emit(fd, "  node [fontname=monospace];");
    dot_emit(fd, "  edge [fontname=monospace, fontsize=10];");
    dot_blank(fd);

    if (n == 1) {
        dot_cycle(fd, &cs[0], "", "  ");
    } else {
        /* หลายวง: แยกเป็น cluster ละวง มีกรอบและหัวข้อของตัวเอง */
        for (size_t k = 0; k < n; k++) {
            snprintf(pfx, sizeof pfx, "C%zu_", k + 1);
            cycle_title(&cs[k], title, sizeof title);
            dot_emit(fd, "  subgraph cluster_%zu {", k + 1);
            dot_emit(fd, "    label=%ccycle %zu — %s%c;", DQ, k + 1, title, DQ);
            dot_cycle(fd, &cs[k], pfx, "    ");
            dot_emit(fd, "  }");
        }
    }

    dot_emit(fd, "}");
    if (close(fd) != 0)
        dd_logf("[dd] WARNING: ปิดไฟล์ %s ไม่สำเร็จ\n", path);

    dd_logf("  Graph : เขียน %s แล้ว   (dot -Tpng %s -o cycle.png)\n", path, path);
}
