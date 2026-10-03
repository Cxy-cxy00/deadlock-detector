/* interpose.c — ยังไม่ implement  (ผู้รับผิดชอบ: คนที่ 1)
 *
 * TODO
 *  - dd_interpose_resolve: dlsym(RTLD_NEXT, "pthread_mutex_lock") ฯลฯ
 *  - pthread_mutex_lock ของเรา:
 *      1) dd_state_wait_begin(tid, m, site)
 *      2) rc = real_mutex_lock(m)            <- ตรงนี้คือจุดที่ค้างจริง
 *      3) dd_state_wait_end(tid, m)
 *         ถ้า rc == 0 -> dd_state_acquired(tid, m, site)
 *  - pthread_mutex_unlock: dd_state_released ก่อนปล่อยจริง
 *  - ระวัง recursion: ถ้าโค้ดภายในของเราเรียก lock เอง จะวนกลับมาที่ตัวเอง
 *    ใช้ธง thread-local (__thread int in_detector) กันไว้
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include "interpose.h"
#include "state.h"

dd_mutex_fn_t real_mutex_lock    = NULL;
dd_mutex_fn_t real_mutex_trylock = NULL;
dd_mutex_fn_t real_mutex_unlock  = NULL;

void dd_interpose_resolve(void) { /* TODO */ }

dd_site_t dd_caller_site(void) {
    dd_site_t s = { NULL, 0, NULL };
    return s;  /* TODO: __builtin_return_address(0) + backtrace */
}

/* ---- ฟังก์ชันที่ไปทับของ libpthread (ยังเป็น stub) ---- */

int pthread_mutex_lock(pthread_mutex_t *m) {
    (void)m;
    return 0;  /* TODO: ตามลำดับ 1-2-3 ด้านบน */
}

int pthread_mutex_trylock(pthread_mutex_t *m) {
    (void)m;
    return 0;  /* TODO: ไม่ต้องลง wait edge เพราะไม่ค้าง */
}

int pthread_mutex_unlock(pthread_mutex_t *m) {
    (void)m;
    return 0;  /* TODO */
}
