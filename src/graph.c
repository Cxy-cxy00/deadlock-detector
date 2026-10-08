/* graph.c — wait-for graph และการหา cycle  (ผู้รับผิดชอบ: คนที่ 3)
 *
 * node = thread,  edge T1 -> T2 แปลว่า "T1 รอ mutex ที่ T2 ถืออยู่"
 * มี cycle = เกิด deadlock แล้ว (ตรงตามนิยามในบท Deadlocks)
 *
 * ===== ข้อสังเกตสำคัญ: กราฟนี้มี out-degree ไม่เกิน 1 =====
 * thread หนึ่งตัวค้างอยู่ใน pthread_mutex_lock ได้ทีละ "หนึ่ง" mutex เท่านั้น
 * (state.c จึงเก็บ wait ได้แถวเดียวต่อ thread) -> แต่ละ node มีลูกศรออกได้ไม่เกินเส้นเดียว
 *
 * ผลคือ DFS ไม่ต้องใช้ recursion หรือ adjacency list เลย — เดินตามโซ่เป็น loop ธรรมดา
 * แต่ยังเป็นอัลกอริทึมระบายสี white/gray/black ตามตำราทุกประการ:
 *   WHITE = ยังไม่เคยไป    GRAY = อยู่ในเส้นทางที่กำลังเดิน    BLACK = ตรวจจบแล้ว
 *   เจอ GRAY ซ้ำ = วนกลับมาที่เส้นทางเดิม = เจอ cycle
 *
 * (ถ้าวันหนึ่งไปดัก rwlock หรือ semaphore ที่รอได้หลายตัวพร้อมกัน out-degree จะเกิน 1
 *  ตอนนั้นต้องเปลี่ยนเป็น adjacency list + DFS แบบ recursive ของจริง)
 *
 * ===== หมายเหตุเรื่อง thread ของ detector เอง =====
 * ไม่ต้องกรองออก เพราะ detector ล็อกตารางผ่าน real_mutex_lock ซึ่งไม่ผ่าน wrapper
 * ของเรา จึงไม่เคยถูกบันทึกลงตารางตั้งแต่แรก
 *
 * ===== ใครเรียกได้ =====
 * ตัวแปรระดับไฟล์ทั้งหมดเป็นของ detector thread คนเดียว ห้ามเรียกจาก thread อื่น
 * และต้องเรียก dd_graph_build() ก่อน dd_graph_find_cycle() เสมอ
 */
#include "graph.h"
#include "state.h"

#define DD_NO_EDGE (-1)

static dd_tid_t   g_tid[DD_MAX_THREADS];      /* node i คือ thread ไหน */
static int        g_next[DD_MAX_THREADS];     /* node i รอ node ไหนอยู่ */
static dd_mutex_t g_wait_on[DD_MAX_THREADS];  /* node i รอ mutex ตัวไหน */
static size_t     g_nnodes = 0;

enum { DD_WHITE = 0, DD_GRAY, DD_BLACK };
static unsigned char g_color[DD_MAX_THREADS];
static int           g_path[DD_MAX_THREADS];  /* เส้นทางที่กำลังเดิน */

static int find_node(dd_tid_t t) {
    for (size_t i = 0; i < g_nnodes; i++)
        if (g_tid[i] == t) return (int)i;
    return DD_NO_EDGE;
}

static int add_node(dd_tid_t t) {
    int i = find_node(t);
    if (i != DD_NO_EDGE) return i;
    if (g_nnodes >= DD_MAX_THREADS) return DD_NO_EDGE;
    i = (int)g_nnodes++;
    g_tid[i]     = t;
    g_next[i]    = DD_NO_EDGE;
    g_wait_on[i] = NULL;
    return i;
}

/* ---- สร้างกราฟจาก snapshot ของตารางใน state.c ----
 * คัดลอกออกมาใต้ dd_state_lock ครั้งเดียวแล้วปล่อยล็อกทันที
 * จะได้ไม่ไปถ่วง thread ของโปรแกรมเป้าหมายระหว่างที่เราเดินกราฟ
 */
void dd_graph_build(void) {
    const dd_wait_t *waits;
    size_t nwaits;

    g_nnodes = 0;

    dd_state_lock();
    nwaits = dd_state_waits(&waits);
    for (size_t i = 0; i < nwaits; i++) {
        dd_tid_t holder;

        /* mutex ที่ไม่มีเจ้าของ = ไม่เกิด edge (thread กำลังจะได้อยู่แล้ว) */
        if (!dd_state_holder_of(waits[i].mutex, &holder))
            continue;

        int u = add_node(waits[i].waiter);
        int v = add_node(holder);
        if (u == DD_NO_EDGE || v == DD_NO_EDGE)
            continue;                        /* node เต็ม — ข้ามไป ไม่ crash */

        g_next[u]    = v;
        g_wait_on[u] = waits[i].mutex;

        /* u == v แปลว่า thread รอ mutex ที่ตัวเองถืออยู่ = self-deadlock
         * (ล็อก mutex ตัวเดิมซ้ำโดยที่ไม่ใช่ชนิด recursive) — เป็น cycle ความยาว 1
         */
        dd_dbgf("graph: edge tid %lu -> tid %lu (รอ mutex %p)\n",
                waits[i].waiter, holder, waits[i].mutex);
    }
    dd_state_unlock();

    dd_dbgf("graph: สร้างเสร็จ %zu node\n", g_nnodes);
}

/* ---- หา cycle ด้วย DFS ระบายสี ----
 * คืน 1 ถ้าเจอ (เขียนผลลง out) คืน 0 ถ้าไม่เจอ
 */
int dd_graph_find_cycle(dd_cycle_t *out) {
    if (out == NULL || g_nnodes == 0)
        return 0;

    for (size_t i = 0; i < g_nnodes; i++)
        g_color[i] = DD_WHITE;

    for (size_t s = 0; s < g_nnodes; s++) {
        if (g_color[s] != DD_WHITE)
            continue;

        /* เดินตามโซ่จาก s ระบาย GRAY ไปเรื่อย ๆ จนกว่าจะตัน หรือเจอสีอื่น */
        size_t plen = 0;
        int u = (int)s;
        while (u != DD_NO_EDGE && g_color[u] == DD_WHITE) {
            g_color[u] = DD_GRAY;
            g_path[plen++] = u;
            u = g_next[u];
        }

        if (u != DD_NO_EDGE && g_color[u] == DD_GRAY) {
            /* เจอ GRAY = วนกลับเข้าเส้นทางที่กำลังเดินอยู่ -> เป็น cycle แน่นอน
             * (GRAY ต้องมาจากรอบนี้เสมอ เพราะจบแต่ละรอบเราทา BLACK ทิ้งทั้งเส้น)
             *
             * ตัดเอาเฉพาะช่วงที่เป็นวงจริง ๆ ส่วนหัวที่ห้อยเข้ามาไม่ใช่ส่วนของ cycle
             *   เช่น T4 -> T1 -> T2 -> T3 -> T1  วงคือ [T1 T2 T3] ไม่เอา T4
             */
            size_t start = 0;
            while (start < plen && g_path[start] != u)
                start++;

            size_t len = plen - start;
            if (len > DD_MAX_CYCLE_LEN) {
                dd_logf("[dd] WARNING: cycle ยาว %zu เกิน DD_MAX_CYCLE_LEN (%d) "
                        "— รายงานแค่บางส่วน\n", len, DD_MAX_CYCLE_LEN);
                len = DD_MAX_CYCLE_LEN;
            }

            for (size_t k = 0; k < len; k++) {
                int n = g_path[start + k];
                out->tids[k]    = g_tid[n];
                out->mutexes[k] = g_wait_on[n];   /* mutex ที่ tids[k] รออยู่ */
            }
            out->len = len;

            dd_dbgf("graph: เจอ cycle ยาว %zu\n", len);
            return 1;
        }

        /* เส้นทางนี้ตันหรือไปชนของเก่าที่ตรวจแล้ว -> ไม่มี cycle ผ่านทางนี้ */
        for (size_t k = 0; k < plen; k++)
            g_color[g_path[k]] = DD_BLACK;
    }

    return 0;
}
