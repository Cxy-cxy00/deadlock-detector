/* report.c — พิมพ์รายงานและ export Graphviz  (ผู้รับผิดชอบ: คนที่ 5)
 *
 * ฟังก์ชันในไฟล์นี้ถูกเรียกจาก detector thread ซึ่งไม่ได้อยู่ใน lock path ของ
 * โปรแกรมเป้าหมาย จึงใช้ buffer บน stack และ snprintf ได้สบาย ๆ
 * แต่ยังต้องออกทาง dd_logf (write(2,...) ตรง ๆ) ไม่ใช่ printf
 * เพราะ stdio ของโปรแกรมเป้าหมายอาจค้างคาอยู่ตอนที่เรากำลังจะรายงาน
 */
#include <stdio.h>    /* snprintf */
#include "report.h"
#include "state.h"

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

/* file:line ถ้ามี, ไม่งั้นโชว์ pc ดิบไว้ก่อน (ขั้น 9 จะแปลงให้เป็นบรรทัด) */
static void print_line(const char *label, dd_mutex_t m, const dd_site_t *s) {
    if (s != NULL && s->file != NULL)
        dd_logf("      %-7s mutex %p   at %s:%d\n", label, m, s->file, s->line);
    else if (s != NULL && s->pc != NULL)
        dd_logf("      %-7s mutex %p   at pc %p\n", label, m, s->pc);
    else
        dd_logf("      %-7s mutex %p\n", label, m);
}

void dd_report_cycle(const dd_cycle_t *c) {
    char buf[1024];
    int  off;

    if (c == NULL || c->len == 0) return;

    dd_logf("\n[DEADLOCK DETECTED] %zu threads in cycle\n\n", c->len);

    dd_state_lock();
    for (size_t i = 0; i < c->len; i++) {
        /* tids[i] รอ mutexes[i] ซึ่งถือโดย tids[i+1]
         * ดังนั้น tids[i] ย่อมถือ mutexes[i-1] คือตัวที่คนก่อนหน้าในวงกำลังรออยู่
         * -> ข้อมูลทั้งสองบรรทัดอนุมานได้จาก dd_cycle_t ล้วน ๆ
         */
        dd_mutex_t waiting = c->mutexes[i];
        dd_mutex_t held    = c->mutexes[(i + c->len - 1) % c->len];

        dd_logf("  Thread %lu\n", c->tids[i]);
        print_line("holds",   held,    site_of_hold(held));
        print_line("waiting", waiting, site_of_wait(c->tids[i]));
    }
    dd_state_unlock();

    /* ประกอบบรรทัด Cycle ในบัฟเฟอร์เดียวแล้วพิมพ์ทีเดียว จะได้ไม่ถูกแทรกกลาง */
    off = snprintf(buf, sizeof buf, "\n  Cycle: ");
    for (size_t i = 0; i < c->len && off > 0 && off < (int)sizeof buf; i++)
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

void dd_report_resolve_site(dd_site_t *site) {
    (void)site;
    /* TODO ขั้น 9: แปลง site->pc เป็น file:line
     * ระวัง: pc เป็นที่อยู่ตอนรัน ไม่ใช่ offset ในไฟล์ (bank เป็น PIE + มี ASLR)
     * ต้องลบ base address ของ module ออกก่อน — ใช้ dladdr() เอา dli_fbase
     * แล้วค่อยส่ง offset ให้ addr2line
     */
}
