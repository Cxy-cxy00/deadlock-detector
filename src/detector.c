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

/* ---- lock-order inversion ----
 * state.c จดคำเตือนเข้าคิวตั้งแต่ใน lock path  detector แค่หยิบมาพิมพ์
 * static เพราะมีแต่ detector thread (หรือ destructor หลัง thread หยุดแล้ว) ที่แตะ
 */
static dd_inversion_t g_inv[DD_MAX_INVERSIONS];

static int cycle_has_mutex(const dd_cycle_t *c, dd_mutex_t m) {
    for (size_t i = 0; i < c->len; i++)
        if (c->mutexes[i] == m) return 1;
    return 0;
}

/* inversion ที่ mutex ทั้งคู่อยู่ในวง deadlock รอบนี้อยู่แล้ว = ค้างไปแล้วจริง
 * รายงาน [DEADLOCK DETECTED] บอกมากกว่าอยู่แล้ว จึงไม่เตือนซ้ำ
 * (bank.c ได้รายงานหน้าตาเดิม ไม่มีคำเตือนแทรก)
 */
static int covered_by_cycle(const dd_inversion_t *v, const dd_cycle_t *cs, size_t n) {
    for (size_t k = 0; k < n; k++)
        if (cycle_has_mutex(&cs[k], v->seen.first) && cycle_has_mutex(&cs[k], v->seen.second))
            return 1;
    return 0;
}

static void report_inversions(size_t k, const dd_cycle_t *cs, size_t n) {
    for (size_t i = 0; i < k; i++) {
        if (covered_by_cycle(&g_inv[i], cs, n)) {
            dd_dbgf("detector: inversion อยู่ในวง deadlock แล้ว ไม่เตือนซ้ำ\n");
            continue;
        }
        dd_report_inversion(&g_inv[i]);
    }
}

/* ตรวจหนึ่งรอบ: หยิบ inversion -> สร้างกราฟ -> หา cycle ทุกวง -> รายงานเฉพาะวงใหม่
 * คืน 1 ถ้า "ตอนนี้มี deadlock" แม้จะเป็นวงเดิมที่รายงานไปแล้วก็ตาม
 */
int dd_detector_check_once(void) {
    size_t n, k, fresh = 0;

    /* หยิบ inversion "ก่อน" ถ่าย snapshot กราฟ — inversion ที่เกิดจาก deadlock จริง
     * มีวงอยู่แล้วตั้งแต่ตอนที่ถูกจด และวง deadlock ไม่มีวันหายเอง snapshot ที่ถ่ายทีหลังจึงเห็นวงนั้นแน่
     * ถ้าสลับลำดับ inversion อาจเข้าคิวหลัง snapshot แล้วถูกพิมพ์ทั้งที่เป็นส่วนของ deadlock
     */
    k = dd_state_take_inversions(g_inv, DD_MAX_INVERSIONS);

    dd_graph_build();
    n = dd_graph_find_cycles(g_cur, DD_MAX_CYCLES);

    report_inversions(k, g_cur, n);

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

    /* โปรแกรมที่จบเร็วกว่าคาบเดียวของ detector จะไม่ทันได้พิมพ์คำเตือนในคิว
     * (lock-order inversion มีประโยชน์ที่สุดกับโปรแกรมที่ "ยังไม่ค้าง" ซึ่งก็คือโปรแกรมที่จบเองนี่แหละ)
     * detector thread หยุดแล้ว ตรงนี้จึงใช้ g_inv ได้คนเดียว  ไม่มี deadlock ให้กรองเพราะโปรแกรมจบได้
     */
    report_inversions(dd_state_take_inversions(g_inv, DD_MAX_INVERSIONS), NULL, 0);

    dd_state_fini();
}
