/* detector.h — detector thread และการตรวจ lock-order inversion
 * (ผู้รับผิดชอบ: คนที่ 4)
 *
 * detector เป็น thread แยกของเราเอง จึงยังทำงานได้ทั้งที่ thread อื่นค้างหมดแล้ว
 */
#ifndef DD_DETECTOR_H
#define DD_DETECTOR_H

#include "common.h"

/* สร้าง/หยุด detector thread — เรียกจาก constructor/destructor */
void dd_detector_start(void);
void dd_detector_stop(void);

/* ตรวจรอบเดียว: build graph -> find cycle -> report. คืน 1 ถ้าเจอ deadlock */
int dd_detector_check_once(void);

/* เตือนล่วงหน้าแบบ prevention: thread ล็อกสองตัวสลับลำดับกับที่เคยเห็น
 * (ยังไม่ค้าง แต่เสี่ยง) — เรียกจาก dd_state_acquired
 */
void dd_detector_note_order(dd_tid_t t, dd_mutex_t prev, dd_mutex_t next);

#endif /* DD_DETECTOR_H */
