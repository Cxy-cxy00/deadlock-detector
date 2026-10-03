/* state.c — ยังไม่ implement  (ผู้รับผิดชอบ: คนที่ 2)
 *
 * TODO
 *  - เลือกโครงสร้างข้อมูล: array ขนาดคงที่ หรือ open-addressing hash บน dd_mutex_t
 *  - ห้ามใช้ malloc ใน path ของ lock ถ้าเลี่ยงได้ (จะ re-enter ตัวเอง)
 *  - ห้ามใช้ pthread_mutex_* ตัวที่เรา interpose มาป้องกันตาราง ต้องเรียก real_* ตรง ๆ
 */
#include "state.h"

void dd_state_init(void) { /* TODO */ }
void dd_state_fini(void) { /* TODO */ }

void dd_state_wait_begin(dd_tid_t t, dd_mutex_t m, dd_site_t site) { (void)t; (void)m; (void)site; /* TODO */ }
void dd_state_wait_end  (dd_tid_t t, dd_mutex_t m)                 { (void)t; (void)m; /* TODO */ }
void dd_state_acquired  (dd_tid_t t, dd_mutex_t m, dd_site_t site) { (void)t; (void)m; (void)site; /* TODO */ }
void dd_state_released  (dd_tid_t t, dd_mutex_t m)                 { (void)t; (void)m; /* TODO */ }

void dd_state_lock(void)   { /* TODO */ }
void dd_state_unlock(void) { /* TODO */ }

size_t dd_state_waits(const dd_wait_t **out) { (void)out; return 0; /* TODO */ }
size_t dd_state_holds(const dd_hold_t **out) { (void)out; return 0; /* TODO */ }
int dd_state_holder_of(dd_mutex_t m, dd_tid_t *out) { (void)m; (void)out; return 0; /* TODO */ }
