/* log.c — พิมพ์ข้อความของเราออก stderr โดยไม่ไปยุ่งกับ stdio ของโปรแกรมเป้าหมาย
 *
 * ทำไมห้ามใช้ printf / fprintf ในโปรเจ็คนี้:
 *   1. buffer     stdio มี buffer ของตัวเอง  เป้าหมาย deadlock แล้วไม่จบ = ไม่ flush
 *                 รายงานจะค้างอยู่ใน buffer ไม่มีใครได้เห็น  write(2,...) ไม่ผ่าน buffer
 *   2. lock       FILE* ทุกตัวมี lock ภายใน  ถ้า thread ของเป้าหมายค้างคาขณะถือ lock นั้น
 *                 detector ของเราจะค้างตามไปด้วย แล้วไม่ได้รายงานอะไรเลย
 *                 (เครื่องมือตายในสถานการณ์ที่มันถูกสร้างมาเพื่อแก้พอดี)
 *   3. recursion  printf เรียก lock ภายใน ซึ่งอาจวนกลับเข้า pthread_mutex_lock ที่เราทับไว้
 *   4. ไปปนกัน    ใช้ fd 2 แยกจาก stdout ของเป้าหมาย  แยก redirect ได้ (./prog 2>dd.log)
 *
 * กฎเหล็ก: ห้าม log ทศนิยม (%f %g) เพราะ vsnprintf ของ glibc อาจ malloc ข้างใน
 *          ซึ่งผิดกฎ "ห้าม malloc ใน lock path"  ใช้แค่ %s %d %ld %lu %p %zu
 */
#include <stdarg.h>
#include <stdio.h>    /* vsnprintf, snprintf */
#include <stdlib.h>   /* getenv */
#include <string.h>   /* strcmp */
#include <unistd.h>   /* write */
#include <errno.h>
#include "common.h"

#define DD_LOG_BUF 512   /* อยู่บน stack — ห้าม malloc */

static int g_verbose = 0;

/* constructor priority 101 = คิวแรกสุดที่โค้ดผู้ใช้จองได้ (0-100 สงวนให้ implementation)
 * ต้องรันก่อน constructor ของ detector.c เพราะที่นั่นอาจเรียก dd_dbgf แล้ว
 * getenv ตรงนี้ปลอดภัย — ยังไม่ใช่ lock path
 */
__attribute__((constructor(101)))
static void dd_log_init(void) {
    const char *v = getenv("DD_VERBOSE");
    g_verbose = (v != NULL && *v != '\0' && strcmp(v, "0") != 0);
}

/* เขียนให้ครบทั้งก้อนด้วย write ครั้งเดียวต่อหนึ่งบรรทัด
 * ถ้าแยกเขียนหลายครั้ง (prefix / ข้อความ / newline) บรรทัดจาก thread อื่นจะแทรกกลาง
 */
static void dd_write_all(const char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(2, buf + off, len - off);
        if (w <= 0) {
            if (w < 0 && errno == EINTR) continue;
            break;              /* เขียนไม่ได้ก็ยอมแพ้ ห้ามวนไม่รู้จบ */
        }
        off += (size_t)w;
    }
}

static void dd_vlogf(const char *prefix, const char *fmt, va_list ap) {
    char buf[DD_LOG_BUF];
    int off = 0;

    if (prefix != NULL) {
        off = snprintf(buf, sizeof buf, "%s", prefix);
        if (off < 0) return;
        if (off > (int)sizeof buf - 1) off = (int)sizeof buf - 1;
    }

    int n = vsnprintf(buf + off, sizeof buf - (size_t)off, fmt, ap);
    if (n < 0) return;

    /* vsnprintf คืน "ความยาวที่ควรจะใช้" ไม่ใช่ที่เขียนได้จริง — ยาวเกินก็ตัด */
    int total = off + n;
    if (total > (int)sizeof buf - 1) total = (int)sizeof buf - 1;

    dd_write_all(buf, (size_t)total);
}

/* พิมพ์เสมอ ไม่เติม prefix — ใช้กับรายงาน [DEADLOCK DETECTED] ที่มีรูปแบบของตัวเอง */
void dd_logf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    dd_vlogf(NULL, fmt, ap);
    va_end(ap);
}

/* พิมพ์เฉพาะตอนตั้ง DD_VERBOSE — ใช้ debug ระหว่างพัฒนา เติม "[dd] " ให้เอง
 * ปิดอยู่ = คืนทันทีโดยไม่เสียเวลา format อะไรเลย
 */
void dd_dbgf(const char *fmt, ...) {
    if (!g_verbose) return;
    va_list ap;
    va_start(ap, fmt);
    dd_vlogf("[dd] ", fmt, ap);
    va_end(ap);
}
