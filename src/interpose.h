/* interpose.h — ตัวชี้ไปยังฟังก์ชันตัวจริงใน libpthread  (ผู้รับผิดชอบ: คนที่ 1)
 *
 * เราตั้งชื่อฟังก์ชันซ้ำกับของ libpthread แล้วให้ LD_PRELOAD โหลด .so ของเราก่อน
 * ตัวจริงยังเรียกได้ผ่าน dlsym(RTLD_NEXT, "ชื่อฟังก์ชัน")
 */
#ifndef DD_INTERPOSE_H
#define DD_INTERPOSE_H

#include "common.h"

typedef int (*dd_mutex_fn_t)(pthread_mutex_t *);

extern dd_mutex_fn_t real_mutex_lock;
extern dd_mutex_fn_t real_mutex_trylock;
extern dd_mutex_fn_t real_mutex_unlock;

/* resolve ตัวจริงทั้งหมด — เรียกครั้งเดียวจาก constructor */
void dd_interpose_resolve(void);

/* thread id ที่ใช้รายงาน — เป็น kernel TID ตรงกับที่ ps -L / top -H / gdb แสดง */
dd_tid_t dd_self(void);

/* ---- เก็บ return address ของผู้เรียก เพื่อ map เป็น file:line ภายหลัง ----
 *
 * ต้องใช้ผ่าน "มาโคร" DD_CALLER_SITE() ไม่ใช่เรียกฟังก์ชันตรง ๆ
 * เพราะ __builtin_return_address(0) ต้องถูก expand อยู่ใน wrapper เอง
 * ถึงจะได้ที่อยู่ในโปรแกรมเป้าหมาย  ถ้าไปเรียกข้างใน dd_site_from_pc
 * จะได้ที่อยู่ของ wrapper ของเราเองแทน ซึ่งไม่มีประโยชน์
 *
 * (ใช้ __builtin_return_address(1) แทนไม่ได้ เพราะ -O2 ตัด frame pointer ทิ้ง
 *  ค่าที่ได้อาจเพี้ยนหรือทำให้ crash — gcc เตือนไว้ในเอกสารเอง)
 */
dd_site_t dd_site_from_pc(void *pc);
#define DD_CALLER_SITE()  dd_site_from_pc(__builtin_return_address(0))

#endif /* DD_INTERPOSE_H */
