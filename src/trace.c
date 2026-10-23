/* trace.c — บันทึกเหตุการณ์ทีละขั้นลงไฟล์ (env DD_TRACE) ให้หน้า playground เล่นเป็น timeline
 *
 * รายงาน [DEADLOCK DETECTED] บอกแค่ "ตอนจบ" ว่าใครค้างกับใคร
 * ไฟล์นี้จดทุกจังหวะระหว่างทาง: ใครขอ ใครได้ ใครปล่อย edge เกิดตอนไหน
 * และ detector ตื่นมาตรวจกี่รอบก่อนจะเจอวง  หน้าเว็บเอาไปเล่นย้อนทีละขั้น
 *
 * ===== กฎเดียวกับ log.c =====
 * dd_trace_lock_event ถูกเรียกใน thread ของโปรแกรมเป้าหมาย (ผ่าน state.c)
 *   - ห้าม malloc / printf / stdio  ใช้ buffer บน stack + write(2) ครั้งเดียวต่อบรรทัด
 *   - ห้าม log ทศนิยม  เวลาเก็บเป็นไมโครวินาทีจำนวนเต็ม
 *   - clock_gettime(CLOCK_MONOTONIC) ไปทาง vDSO ไม่มี lock
 *
 * ===== ไฟล์ถูกเขียนทับโดย process ลูก =====
 * LD_PRELOAD ตกทอดไปยัง process ลูก รวมถึง sh/addr2line ที่ report.c เรียกผ่าน popen
 * ถ้าลูกเห็น DD_TRACE ด้วย constructor ของมันจะเปิดไฟล์เดิมด้วย O_TRUNC แล้วลบ timeline ทิ้ง
 * จึง unsetenv ทันทีหลังอ่าน (ยังอยู่ใน constructor มี thread เดียว แก้ environ ได้ปลอดภัย)
 */
#include <errno.h>
#include <fcntl.h>
#include <link.h>        /* dl_iterate_phdr */
#include <stdatomic.h>
#include <stdio.h>       /* snprintf */
#include <stdlib.h>      /* getenv, unsetenv */
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "trace.h"

/* จำกัดจำนวนเหตุการณ์ lock ที่จด — โปรแกรมที่ล็อกรัว ๆ (เช่น 4 thread × 10,000 รอบ)
 * จะได้ไฟล์หลายสิบ MB ซึ่งหน้าเว็บเล่นไม่ไหว  เหตุการณ์ของ detector ไม่นับรวม
 * (เกิดแค่ 2 ครั้งต่อวินาที และเป็นส่วนที่ต้องเห็นเสมอ)
 */
#define DD_TRACE_MAX_EVENTS 5000

int                    dd_trace_fd = -1;
static struct timespec g_t0;
static atomic_ulong    g_seq     = 0;
static atomic_ulong    g_nlock   = 0;
static atomic_int      g_trunced = 0;
static unsigned long   g_round   = 0;   /* แตะเฉพาะ detector thread */

static unsigned long now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    /* คิดแบบมีเครื่องหมายก่อน — tv_nsec ของตอนนี้อาจน้อยกว่าของ t0 */
    long long us = (long long)(ts.tv_sec - g_t0.tv_sec) * 1000000LL +
                   (long long)(ts.tv_nsec - g_t0.tv_nsec) / 1000;
    return us < 0 ? 0UL : (unsigned long)us;
}

static void write_line(const char *buf, int n) {
    size_t off = 0;
    if (n <= 0) return;
    while (off < (size_t)n) {
        ssize_t w = write(dd_trace_fd, buf + off, (size_t)n - off);
        if (w <= 0) {
            if (w < 0 && errno == EINTR) continue;
            return;                 /* เขียนไม่ได้ก็ยอม ห้ามวนไม่รู้จบ */
        }
        off += (size_t)w;
    }
}

/* snprintf คืน "ความยาวที่ควรได้" ถ้ายาวเกิน buffer ให้ตัดเหลือเท่าที่เขียนจริง */
static int clamp_len(int n, size_t size) {
    if (n < 0) return 0;
    return n >= (int)size ? (int)size - 1 : n;
}

/* callback ตัวแรกของ dl_iterate_phdr คือโปรแกรมหลักเสมอ
 * dlpi_addr = ที่อยู่ที่ถูกโหลด (PIE) หรือ 0 (non-PIE)  ใช้แปลง pc เป็น offset ให้ addr2line
 */
static int first_object(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size;
    *(unsigned long *)data = (unsigned long)info->dlpi_addr;
    return 1;                       /* หยุดหลังตัวแรก */
}

void dd_trace_init(void) {
    const char *path = getenv("DD_TRACE");
    char buf[96];
    unsigned long base = 0;

    if (path == NULL || *path == 0) return;

    dd_trace_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND | O_CLOEXEC, 0644);
    unsetenv("DD_TRACE");           /* กันลูกเขียนทับ — ดูหัวไฟล์ */
    if (dd_trace_fd < 0) {
        dd_logf("[dd] WARNING: เปิดไฟล์ DD_TRACE ไม่ได้ — ไม่มี timeline\n");
        return;
    }
    clock_gettime(CLOCK_MONOTONIC, &g_t0);

    dl_iterate_phdr(first_object, &base);
    write_line(buf, clamp_len(snprintf(buf, sizeof buf, "# dd-trace 1\nbase 0x%lx\n", base),
                              sizeof buf));
}

void dd_trace_lock_event(const char *kind, dd_tid_t t, dd_mutex_t m, void *pc) {
    char buf[160];
    int  n;

    if (dd_trace_fd < 0) return;

    if (atomic_fetch_add(&g_nlock, 1) >= DD_TRACE_MAX_EVENTS) {
        int expected = 0;
        if (atomic_compare_exchange_strong(&g_trunced, &expected, 1)) {
            n = snprintf(buf, sizeof buf, "%lu %lu trunc\n",
                         atomic_fetch_add(&g_seq, 1), now_us());
            write_line(buf, clamp_len(n, sizeof buf));
        }
        return;
    }

    n = snprintf(buf, sizeof buf, "%lu %lu %s %lu 0x%lx 0x%lx\n",
                 atomic_fetch_add(&g_seq, 1), now_us(), kind, t,
                 (unsigned long)m, (unsigned long)pc);
    write_line(buf, clamp_len(n, sizeof buf));
}

void dd_trace_relx(dd_tid_t t, dd_mutex_t m, dd_tid_t holder) {
    char buf[160];

    if (dd_trace_fd < 0) return;
    if (atomic_fetch_add(&g_nlock, 1) >= DD_TRACE_MAX_EVENTS) return;

    write_line(buf, clamp_len(snprintf(buf, sizeof buf, "%lu %lu relx %lu 0x%lx %lu\n",
                                       atomic_fetch_add(&g_seq, 1), now_us(), t,
                                       (unsigned long)m, holder),
                              sizeof buf));
}

void dd_trace_check(size_t nedges) {
    char buf[96];

    if (dd_trace_fd < 0) return;
    g_round++;
    write_line(buf, clamp_len(snprintf(buf, sizeof buf, "%lu %lu check %lu %zu\n",
                                       atomic_fetch_add(&g_seq, 1), now_us(),
                                       g_round, nedges),
                              sizeof buf));
}

/* วงยาวสุด 64 คน × ราว 35 ตัวอักษร — ใหญ่กว่า buffer ของ lock path มาก
 * แต่ฟังก์ชันนี้รันใน detector thread เท่านั้น ใช้ stack 4 KB ได้สบาย
 */
void dd_trace_cycle(const dd_cycle_t *c, int fresh) {
    char buf[4096];
    int  off;

    if (dd_trace_fd < 0 || c == NULL) return;

    off = clamp_len(snprintf(buf, sizeof buf, "%lu %lu cycle %lu %d %zu %zu %d",
                             atomic_fetch_add(&g_seq, 1), now_us(), g_round,
                             fresh ? 1 : 0, c->len, c->full_len, c->partial),
                    sizeof buf);
    for (size_t i = 0; i < c->len && off < (int)sizeof buf - 1; i++)
        off += clamp_len(snprintf(buf + off, sizeof buf - (size_t)off, " %lu:0x%lx",
                                  c->tids[i], (unsigned long)c->mutexes[i]),
                         sizeof buf - (size_t)off);
    if (off > (int)sizeof buf - 2)
        off = (int)sizeof buf - 2;   /* ยาวเกินก็ตัด แต่ต้องจบบรรทัดเสมอ */
    buf[off++] = '\n';
    write_line(buf, off);
}
