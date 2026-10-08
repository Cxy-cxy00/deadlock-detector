/* detector.c — detector thread  (ผู้รับผิดชอบ: คนที่ 4)
 *
 * หัวใจของเรื่องนี้: thread ของโปรแกรมเป้าหมายค้างกันหมดแล้ว แต่ detector เป็น
 * thread ของ "library เรา" ซึ่งไม่ได้ไปแตะ mutex ของเขาเลย จึงยังวิ่งได้ตามปกติ
 * และรายงานออกมาได้  นี่คือเหตุผลที่เครื่องมือนี้ทำงานได้ทั้งที่โปรแกรมตายแล้ว
 *
 * ข้อควรรู้: detector ล็อกตารางผ่าน real_mutex_lock ซึ่งไม่ผ่าน wrapper ของเรา
 * และยังตั้งธง in_dd ค้างไว้ด้วย (dd_interpose_mark_self_internal) เป็นเกราะสองชั้น
 * กันไม่ให้ตัวเองโผล่ไปเป็น node ในกราฟที่กำลังตรวจอยู่
 */
#include <pthread.h>
#include <time.h>
#include "detector.h"
#include "interpose.h"
#include "state.h"
#include "graph.h"
#include "report.h"

/* นอนทีละช่วงสั้น ๆ แทนที่จะนอนยาวครบคาบ เพื่อให้สั่งหยุดแล้วหยุดได้ไว
 * ไม่ต้องรอจนครบ DD_CHECK_INTERVAL_MS ตอนโปรแกรมจบ
 */
#define DD_SLEEP_CHUNK_MS 50

static pthread_t    g_thread;
static int          g_running = 0;
static volatile int g_stop    = 0;

/* cycle ที่รายงานไปแล้ว — ไม่งั้นจะพิมพ์ซ้ำทุกคาบจนจอท่วม
 * (ปิดคำถามข้อสุดท้ายใน docs/design.md: เลือกพิมพ์ครั้งเดียวต่อหนึ่ง cycle)
 */
static dd_cycle_t g_last;
static int        g_has_last = 0;

/* เทียบแบบ "เซต" ไม่ใช่เทียบทีละตำแหน่ง เพราะ DFS อาจเริ่มไล่จากคนละ node
 * แล้วได้ลำดับเริ่มต้นต่างกัน ทั้งที่เป็นวงเดียวกัน
 */
static int same_as_last(const dd_cycle_t *c) {
    if (!g_has_last || g_last.len != c->len) return 0;
    for (size_t i = 0; i < c->len; i++) {
        int found = 0;
        for (size_t j = 0; j < g_last.len; j++)
            if (g_last.tids[j] == c->tids[i]) { found = 1; break; }
        if (!found) return 0;
    }
    return 1;
}

/* ตรวจหนึ่งรอบ: สร้างกราฟ -> หา cycle -> รายงาน
 * คืน 1 ถ้า "ตอนนี้มี deadlock" แม้จะเป็นวงเดิมที่รายงานไปแล้วก็ตาม
 */
int dd_detector_check_once(void) {
    dd_cycle_t c;

    dd_graph_build();
    if (!dd_graph_find_cycle(&c))
        return 0;

    if (same_as_last(&c)) {
        dd_dbgf("detector: วงเดิม ไม่รายงานซ้ำ\n");
        return 1;
    }

    g_last     = c;
    g_has_last = 1;
    dd_report_cycle(&c);
    dd_report_dot(&c, NULL);   /* เขียนไฟล์ .dot ถ้าตั้ง DD_DOT_OUT ไว้ (ขั้น 10) */
    return 1;
}

static void *detector_main(void *arg) {
    (void)arg;

    /* ต้องเป็นบรรทัดแรก — กันไม่ให้ lock ใด ๆ ของ thread นี้ถูกบันทึกลงตาราง */
    dd_interpose_mark_self_internal();

    dd_dbgf("detector: เริ่มทำงาน ตรวจทุก %d ms\n", DD_CHECK_INTERVAL_MS);

    while (!g_stop) {
        for (int slept = 0; slept < DD_CHECK_INTERVAL_MS && !g_stop;
             slept += DD_SLEEP_CHUNK_MS) {
            struct timespec ts = { 0, (long)DD_SLEEP_CHUNK_MS * 1000000L };
            nanosleep(&ts, NULL);
        }
        if (g_stop) break;
        dd_detector_check_once();
    }

    dd_dbgf("detector: หยุดแล้ว\n");
    return NULL;
}

void dd_detector_start(void) {
    if (g_running) return;
    g_stop = 0;
    if (pthread_create(&g_thread, NULL, detector_main, NULL) != 0) {
        dd_logf("[dd] WARNING: สร้าง detector thread ไม่สำเร็จ "
                "— จะไม่มีการตรวจ deadlock\n");
        return;
    }
    g_running = 1;
}

void dd_detector_stop(void) {
    if (!g_running) return;
    g_stop = 1;
    pthread_join(g_thread, NULL);
    g_running = 0;
}

/* ---- ขั้น 11: เตือนล่วงหน้าแบบ prevention ----
 * เก็บคู่ลำดับ (A ก่อน B) ที่เคยเห็น ถ้าวันหนึ่งเจอ (B ก่อน A) = lock-order inversion
 * ยังไม่ค้างแต่เสี่ยง  เป็นส่วนที่เอาไว้เทียบ prevention กับ detection ตอนนำเสนอ
 */
void dd_detector_note_order(dd_tid_t t, dd_mutex_t prev, dd_mutex_t next) {
    (void)t; (void)prev; (void)next;  /* TODO ขั้น 11 */
}

/* ---- จุดเริ่มและจุดจบของ libdetect.so ---- */

__attribute__((constructor))
static void dd_startup(void) {
    dd_interpose_resolve();   /* ต้องมาก่อนเพื่อน — ทุกอย่างพึ่ง real_* */
    dd_state_init();
    dd_detector_start();
}

__attribute__((destructor))
static void dd_shutdown(void) {
    dd_detector_stop();
    dd_state_fini();
}
