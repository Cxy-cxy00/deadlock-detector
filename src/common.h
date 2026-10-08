/* common.h — ค่าคงที่และ helper ที่ใช้ร่วมกันทุกโมดูล */
#ifndef DD_COMMON_H
#define DD_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

/* ---- ขีดจำกัดของตารางสถานะ (ปรับได้) ---- */
#define DD_MAX_THREADS      256
#define DD_MAX_MUTEXES      1024
#define DD_MAX_HELD_PER_THR 32

/* คาบเวลาที่ detector thread ตื่นมาตรวจ wait-for graph (มิลลิวินาที) */
#define DD_CHECK_INTERVAL_MS 500

/* ---- ชนิดข้อมูลพื้นฐาน ---- */
typedef unsigned long dd_tid_t;      /* thread id ที่เราใช้รายงาน */
typedef void          *dd_mutex_t;   /* ที่อยู่ของ pthread_mutex_t ใช้เป็น key */

/* ตำแหน่งใน source ของโปรแกรมเป้าหมาย (ได้จาก backtrace + addr2line) */
typedef struct {
    const char *file;   /* เช่น "bank.c"  — NULL ถ้ายังหาไม่ได้ */
    int         line;   /* เช่น 42        — 0 ถ้ายังหาไม่ได้ */
    void       *pc;     /* return address ดิบ ใช้ resolve ภายหลัง */
} dd_site_t;

/* ---- log ออก stderr โดยไม่ผ่าน stdio ของโปรแกรมเป้าหมาย ---- */
/* พิมพ์เสมอ — ใช้กับรายงาน [DEADLOCK DETECTED] (ไม่เติม prefix ให้) */
void dd_logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* พิมพ์เฉพาะตอนตั้ง env DD_VERBOSE=1 — ใช้ debug ระหว่างพัฒนา เติม "[dd] " ให้เอง
 * ปิดอยู่ = คืนทันที ไม่เสีย overhead  จึงวางไว้ใน lock path ได้
 */
void dd_dbgf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* DD_COMMON_H */
