/* state.c — ตารางสถานะ thread และ mutex  (ผู้รับผิดชอบ: คนที่ 2)
 *
 * ===== ตัดสินใจเรื่องโครงสร้างข้อมูล (ปิดคำถามข้อแรกใน docs/design.md) =====
 * เลือก array ขนาดคงที่ + linear search ไม่ใช่ hash table เพราะ
 *   1. ห้าม malloc ใน lock path เด็ดขาด — malloc มี lock ข้างใน จะวนกลับมาที่เราเอง
 *      array แบบ static จึงเป็นทางเลือกเดียวที่ปลอดภัยจริง ไม่ใช่แค่ "ง่ายกว่า"
 *   2. จำนวนจริงน้อยมาก — bank.c มี 2 mutex โปรแกรมทั่วไปหลักสิบ
 *      ไล่ array ไม่กี่สิบช่องเร็วกว่าคำนวณ hash ด้วยซ้ำ
 *
 * ===== ขนาดตารางมาจากธรรมชาติของปัญหา ไม่ได้เดาเอา =====
 *   waits: 1 thread รอได้ทีละ 1 mutex เท่านั้น -> ไม่เกิน DD_MAX_THREADS แถว
 *   holds: 1 mutex มีเจ้าของได้ทีละ 1 คน      -> ไม่เกิน DD_MAX_MUTEXES แถว
 * ทั้งสองตารางเก็บแบบ "อัดแน่น" (dense) ลบแล้วเอาตัวท้ายมาแทน -> ลบเป็น O(1)
 * และ graph.c วนอ่านได้ตรง ๆ ตั้งแต่ 0 ถึง n โดยไม่ต้องข้ามช่องว่าง
 *
 * ===== ล็อกของตารางเราเอง =====
 * ต้องเรียก real_mutex_lock เท่านั้น ห้ามใช้ pthread_mutex_lock ที่เรา interpose ไว้
 * ไม่งั้นการล็อกตารางจะวนกลับเข้า interpose.c แล้วเรียกกลับมาที่นี่อีก = วนไม่สิ้นสุด
 *
 * ===== ข้อจำกัดที่รู้อยู่ =====
 * ไม่รองรับ PTHREAD_MUTEX_RECURSIVE — thread เดิมล็อกตัวเดิมซ้ำจะนับเป็นครั้งเดียว
 * พอ unlock ครั้งแรกเราจะลบออกจากตารางทั้งที่จริง ๆ ยังถืออยู่
 */
#include "state.h"
#include "interpose.h"

static dd_wait_t g_waits[DD_MAX_THREADS];
static size_t    g_nwaits = 0;

static dd_hold_t g_holds[DD_MAX_MUTEXES];
static size_t    g_nholds = 0;

/* ล็อกคุ้มครองตารางทั้งสอง — static init จึงใช้ได้ทันทีโดยไม่ต้องรอ init() */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/* เตือนเรื่องตารางเต็มแค่ครั้งเดียว ไม่งั้นท่วมจอจนหาอย่างอื่นไม่เจอ */
static int g_warned_waits = 0;
static int g_warned_holds = 0;

/* ---- ล็อก/ปลดล็อกตาราง ----
 * ถ้า real_* ยังเป็น NULL แปลว่าอยู่ช่วงบูตที่ยังมี thread เดียว การไม่ล็อกจึงไม่อันตราย
 */
void dd_state_lock(void) {
    if (real_mutex_lock == NULL) dd_interpose_resolve();
    if (real_mutex_lock != NULL) real_mutex_lock(&g_lock);
}

void dd_state_unlock(void) {
    if (real_mutex_unlock != NULL) real_mutex_unlock(&g_lock);
}

/* ---- ค้นหา: คืน index ที่เจอ หรือคืนจำนวนแถวปัจจุบันถ้าไม่เจอ ----
 * ทั้งสองตัวต้องถูกเรียกขณะถือ g_lock อยู่แล้ว
 */
static size_t find_wait(dd_tid_t t) {
    for (size_t i = 0; i < g_nwaits; i++)
        if (g_waits[i].waiter == t) return i;
    return g_nwaits;
}

static size_t find_hold(dd_mutex_t m) {
    for (size_t i = 0; i < g_nholds; i++)
        if (g_holds[i].mutex == m) return i;
    return g_nholds;
}

/* ---- พิมพ์ตารางทั้งใบ (ต้องถือ g_lock อยู่) ----
 * ใช้ dd_dbgf ล้วน จึงเงียบสนิทเองถ้าไม่ได้ตั้ง DD_VERBOSE
 * บรรทัด EDGE คือสิ่งที่ graph.c จะเอาไปสร้าง wait-for graph ในขั้น 5
 */
static void dump_locked(void) {
    dd_dbgf("---- state: รอ %zu, ถือ %zu ----\n", g_nwaits, g_nholds);
    for (size_t i = 0; i < g_nholds; i++)
        dd_dbgf("     hold  mutex %p  <- tid %lu\n",
                g_holds[i].mutex, g_holds[i].holder);
    for (size_t i = 0; i < g_nwaits; i++) {
        dd_tid_t owner;
        if (dd_state_holder_of(g_waits[i].mutex, &owner))
            dd_dbgf("     wait  tid %lu -> mutex %p (ถือโดย tid %lu)   EDGE %lu -> %lu\n",
                    g_waits[i].waiter, g_waits[i].mutex, owner,
                    g_waits[i].waiter, owner);
        else
            dd_dbgf("     wait  tid %lu -> mutex %p (ยังไม่มีเจ้าของ)\n",
                    g_waits[i].waiter, g_waits[i].mutex);
    }
}

void dd_state_dump(void) {
    dd_state_lock();
    dump_locked();
    dd_state_unlock();
}

/* ---- เตรียม/คืนตาราง ---- */
void dd_state_init(void) {
    dd_state_lock();
    g_nwaits = 0;
    g_nholds = 0;
    dd_state_unlock();
    dd_dbgf("state: พร้อม (รองรับรอสูงสุด %d, mutex สูงสุด %d)\n",
            DD_MAX_THREADS, DD_MAX_MUTEXES);
}

void dd_state_fini(void) {
    /* destructor ตอนโปรแกรมกำลังจะจบ — อ่านดิบ ๆ ได้ ไม่ต้องล็อก */
    dd_dbgf("state: ปิด (ค้างอยู่: รอ %zu, ถือ %zu)\n", g_nwaits, g_nholds);
}

/* ---- บันทึกเหตุการณ์ (เรียกจาก interpose.c) ----
 *
 * ลำดับที่ interpose.c เรียก:
 *   wait_begin -> [real lock ค้างตรงนี้] -> wait_end -> acquired
 * wait_begin ต้องมาก่อนการค้างเสมอ ไม่งั้น thread จะไม่มีโอกาสกลับมาจดอีกเลย
 */

void dd_state_wait_begin(dd_tid_t t, dd_mutex_t m, dd_site_t site) {
    dd_state_lock();
    size_t i = find_wait(t);
    if (i == g_nwaits) {
        if (g_nwaits >= DD_MAX_THREADS) {
            if (!g_warned_waits) {
                g_warned_waits = 1;
                dd_logf("[dd] WARNING: ตาราง waits เต็ม (%d แถว) — "
                        "เพิ่ม DD_MAX_THREADS ใน common.h\n", DD_MAX_THREADS);
            }
            dd_state_unlock();
            return;
        }
        g_nwaits++;
    }
    g_waits[i].waiter = t;
    g_waits[i].mutex  = m;
    g_waits[i].site   = site;

    dump_locked();   /* เงียบเองถ้าไม่ได้ตั้ง DD_VERBOSE */
    dd_state_unlock();
}

void dd_state_wait_end(dd_tid_t t, dd_mutex_t m) {
    dd_state_lock();
    size_t i = find_wait(t);
    if (i < g_nwaits) {
        if (g_waits[i].mutex != m)
            dd_dbgf("WARNING: wait_end tid %lu mutex ไม่ตรงกับที่จดไว้\n", t);
        g_waits[i] = g_waits[g_nwaits - 1];   /* เอาตัวท้ายมาแทน = ลบ O(1) */
        g_nwaits--;
    }
    dd_state_unlock();
}

void dd_state_acquired(dd_tid_t t, dd_mutex_t m, dd_site_t site) {
    dd_state_lock();
    size_t i = find_hold(m);
    if (i == g_nholds) {
        if (g_nholds >= DD_MAX_MUTEXES) {
            if (!g_warned_holds) {
                g_warned_holds = 1;
                dd_logf("[dd] WARNING: ตาราง holds เต็ม (%d แถว) — "
                        "เพิ่ม DD_MAX_MUTEXES ใน common.h\n", DD_MAX_MUTEXES);
            }
            dd_state_unlock();
            return;
        }
        g_nholds++;
    }
    g_holds[i].mutex  = m;
    g_holds[i].holder = t;
    g_holds[i].site   = site;
    dd_state_unlock();
}

void dd_state_released(dd_tid_t t, dd_mutex_t m) {
    dd_state_lock();
    size_t i = find_hold(m);
    if (i < g_nholds) {
        if (g_holds[i].holder != t)
            dd_dbgf("WARNING: tid %lu ปลด mutex %p ที่ tid %lu ถืออยู่\n",
                    t, m, g_holds[i].holder);
        g_holds[i] = g_holds[g_nholds - 1];
        g_nholds--;
    }
    dd_state_unlock();
}

/* ---- อ่านสถานะ — ผู้เรียกต้องถือ dd_state_lock() คร่อมไว้เอง ----
 * คืน pointer ชี้เข้าตารางจริงโดยไม่ copy เพื่อไม่ต้อง malloc
 * ห้ามเก็บ pointer นี้ไว้ใช้ข้ามช่วงที่ปลดล็อกแล้ว
 */
size_t dd_state_waits(const dd_wait_t **out) {
    if (out) *out = g_waits;
    return g_nwaits;
}

size_t dd_state_holds(const dd_hold_t **out) {
    if (out) *out = g_holds;
    return g_nholds;
}

int dd_state_holder_of(dd_mutex_t m, dd_tid_t *out) {
    size_t i = find_hold(m);
    if (i == g_nholds) return 0;
    if (out) *out = g_holds[i].holder;
    return 1;
}
