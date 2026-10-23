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
#include <stdatomic.h>
#include <time.h>
#include "detector.h"
#include "interpose.h"
#include "state.h"
#include "graph.h"
#include "report.h"
#include "trace.h"

/* นอนทีละช่วงสั้น ๆ แทนที่จะนอนยาวครบคาบ เพื่อให้สั่งหยุดแล้วหยุดได้ไว
 * ไม่ต้องรอจนครบ DD_CHECK_INTERVAL_MS ตอนโปรแกรมจบ
 */
#define DD_SLEEP_CHUNK_MS 50

static pthread_t    g_thread;
static int          g_running = 0;

/* destructor (main thread) เขียน, detector thread อ่าน — ต้องเป็น atomic
 * volatile แค่กัน compiler cache ค่า ไม่ใช่ synchronization ตาม C11 จึงยังเป็น data race
 */
static atomic_int   g_stop    = 0;

/* cycle ที่เจอในรอบนี้ และที่เจอในรอบก่อน (= รายงานไปแล้ว)
 * เทียบกับรอบก่อนเพื่อพิมพ์ครั้งเดียวต่อหนึ่ง cycle ไม่งั้นจะพิมพ์ซ้ำทุกคาบจนจอท่วม
 * static เพราะใหญ่ (ราว 1 KB ต่อวง) และมีแต่ detector thread ที่แตะ
 */
static dd_cycle_t g_cur[DD_MAX_CYCLES];
static dd_cycle_t g_seen[DD_MAX_CYCLES];
static size_t     g_nseen = 0;

/* เทียบแบบ "เซต" ไม่ใช่เทียบทีละตำแหน่ง เพราะ DFS อาจเริ่มไล่จากคนละ node
 * แล้วได้ลำดับเริ่มต้นต่างกัน ทั้งที่เป็นวงเดียวกัน
 */
static int same_cycle(const dd_cycle_t *a, const dd_cycle_t *b) {
    if (a->len != b->len) return 0;
    for (size_t i = 0; i < a->len; i++) {
        int found = 0;
        for (size_t j = 0; j < b->len; j++)
            if (b->tids[j] == a->tids[i]) { found = 1; break; }
        if (!found) return 0;
    }
    return 1;
}

static int seen_before(const dd_cycle_t *c) {
    for (size_t i = 0; i < g_nseen; i++)
        if (same_cycle(c, &g_seen[i])) return 1;
    return 0;
}

/* ตรวจหนึ่งรอบ: สร้างกราฟ -> หา cycle ทุกวง -> รายงานเฉพาะวงใหม่
 * คืน 1 ถ้า "ตอนนี้มี deadlock" แม้จะเป็นวงเดิมที่รายงานไปแล้วก็ตาม
 */
int dd_detector_check_once(void) {
    size_t n, fresh = 0;

    dd_graph_build();
    n = dd_graph_find_cycles(g_cur, DD_MAX_CYCLES);

    for (size_t i = 0; i < n; i++) {
        int old = seen_before(&g_cur[i]);
        dd_trace_cycle(&g_cur[i], !old);
        if (old) {
            dd_dbgf("detector: วงเดิม ไม่รายงานซ้ำ\n");
            continue;
        }
        dd_report_cycle(&g_cur[i]);
        fresh++;
    }

    /* มีวงใหม่ -> เขียน .dot ใหม่ทั้งไฟล์ให้มีทุกวงที่ค้างอยู่ตอนนี้ (ถ้าตั้ง DD_DOT_OUT) */
    if (fresh > 0)
        dd_report_dot(g_cur, n, NULL);

    for (size_t i = 0; i < n; i++)
        g_seen[i] = g_cur[i];
    g_nseen = n;

    return n > 0;
}

static void *detector_main(void *arg) {
    (void)arg;

    /* ต้องเป็นบรรทัดแรก — กันไม่ให้ lock ใด ๆ ของ thread นี้ถูกบันทึกลงตาราง */
    dd_interpose_mark_self_internal();

    dd_dbgf("detector: เริ่มทำงาน ตรวจทุก %d ms\n", DD_CHECK_INTERVAL_MS);

    while (!atomic_load(&g_stop)) {
        for (int slept = 0; slept < DD_CHECK_INTERVAL_MS && !atomic_load(&g_stop);
             slept += DD_SLEEP_CHUNK_MS) {
            struct timespec ts = { 0, (long)DD_SLEEP_CHUNK_MS * 1000000L };
            nanosleep(&ts, NULL);
        }
        if (atomic_load(&g_stop)) break;
        dd_detector_check_once();
    }

    dd_dbgf("detector: หยุดแล้ว\n");
    return NULL;
}

void dd_detector_start(void) {
    if (g_running) return;
    atomic_store(&g_stop, 0);
    if (pthread_create(&g_thread, NULL, detector_main, NULL) != 0) {
        dd_logf("[dd] WARNING: สร้าง detector thread ไม่สำเร็จ "
                "— จะไม่มีการตรวจ deadlock\n");
        return;
    }
    g_running = 1;
}

void dd_detector_stop(void) {
    if (!g_running) return;
    atomic_store(&g_stop, 1);
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
    dd_trace_init();          /* ก่อน state/detector จะได้ไม่พลาดเหตุการณ์แรก */
    dd_state_init();
    dd_detector_start();
}

__attribute__((destructor))
static void dd_shutdown(void) {
    dd_detector_stop();
    dd_state_fini();
}
