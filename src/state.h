/* state.h — ตารางสถานะ thread และ mutex  (ผู้รับผิดชอบ: คนที่ 2)
 *
 * เก็บว่าใครถือ mutex ตัวไหน และใครกำลังรอ mutex ตัวไหน
 * ทุกฟังก์ชันในไฟล์นี้ถูกเรียกจาก interpose.c และอ่านโดย graph.c
 */
#ifndef DD_STATE_H
#define DD_STATE_H

#include "common.h"

/* หนึ่งรายการ = "thread หนึ่งตัวกำลังรอ mutex หนึ่งตัว" */
typedef struct {
    dd_tid_t   waiter;      /* thread ที่กำลังรอ */
    dd_mutex_t mutex;       /* mutex ที่รออยู่ */
    dd_site_t  site;        /* บรรทัดที่เรียก lock */
} dd_wait_t;

/* หนึ่งรายการ = "mutex หนึ่งตัวถูก thread หนึ่งตัวถืออยู่" */
typedef struct {
    dd_mutex_t mutex;
    dd_tid_t   holder;
    dd_site_t  site;        /* บรรทัดที่ล็อกสำเร็จ */
} dd_hold_t;

/* เตรียม/คืนตาราง — เรียกจาก constructor/destructor ของ libdetect.so */
void dd_state_init(void);
void dd_state_fini(void);

/* --- บันทึกเหตุการณ์ (เรียกจาก interpose.c) --- */
void dd_state_wait_begin(dd_tid_t t, dd_mutex_t m, dd_site_t site);
void dd_state_wait_end  (dd_tid_t t, dd_mutex_t m);
void dd_state_acquired  (dd_tid_t t, dd_mutex_t m, dd_site_t site);
void dd_state_released  (dd_tid_t t, dd_mutex_t m);

/* --- อ่านสถานะ (เรียกจาก graph.c / report.c) ---
 * ต้องถือ dd_state_lock()/dd_state_unlock() คร่อมไว้ตอนอ่าน
 */
void dd_state_lock(void);
void dd_state_unlock(void);

size_t dd_state_waits (const dd_wait_t **out);   /* คืนจำนวน + ชี้ไปที่ array */
size_t dd_state_holds (const dd_hold_t **out);
int    dd_state_holder_of(dd_mutex_t m, dd_tid_t *out);  /* 1 = เจอ, 0 = ว่าง */

#endif /* DD_STATE_H */
