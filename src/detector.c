/* detector.c — ยังไม่ implement  (ผู้รับผิดชอบ: คนที่ 4)
 *
 * TODO
 *  - dd_detector_start: สร้าง thread ที่วน nanosleep(DD_CHECK_INTERVAL_MS) แล้วเรียก check_once
 *  - dd_detector_check_once: dd_graph_build() -> dd_graph_find_cycle() -> dd_report_cycle()
 *  - dd_detector_note_order: เก็บคู่ลำดับ (A ก่อน B) ที่เคยเห็น ถ้าเจอ (B ก่อน A) = inversion
 *    นี่คือส่วน prevention ที่ใช้เทียบกับ detection ในการนำเสนอ
 *
 * ---- จุดเริ่มทำงานของ libdetect.so ----
 */
#include "detector.h"
#include "interpose.h"
#include "state.h"

__attribute__((constructor))
static void dd_startup(void) {
    dd_interpose_resolve();
    dd_state_init();
    dd_detector_start();
}

__attribute__((destructor))
static void dd_shutdown(void) {
    dd_detector_stop();
    dd_state_fini();
}

void dd_detector_start(void) { /* TODO */ }
void dd_detector_stop(void)  { /* TODO */ }

int dd_detector_check_once(void) { return 0; /* TODO */ }

void dd_detector_note_order(dd_tid_t t, dd_mutex_t prev, dd_mutex_t next) {
    (void)t; (void)prev; (void)next;  /* TODO */
}
