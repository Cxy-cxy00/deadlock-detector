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

/* ตรวจรอบเดียว: พิมพ์ lock-order inversion ที่รออยู่ -> build graph -> find cycle -> report
 * คืน 1 ถ้าเจอ deadlock
 * (การจดลำดับการล็อกอยู่ใน state.c ตอน wait_begin — ที่นี่แค่หยิบคำเตือนไปพิมพ์)
 */
int dd_detector_check_once(void);

#endif /* DD_DETECTOR_H */
