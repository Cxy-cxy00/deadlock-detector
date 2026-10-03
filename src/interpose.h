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

/* เก็บ return address ของผู้เรียก เพื่อ map เป็น file:line ภายหลัง */
dd_site_t dd_caller_site(void);

#endif /* DD_INTERPOSE_H */
