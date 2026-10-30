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
#include "trace.h"

static dd_wait_t g_waits[DD_MAX_THREADS];
static size_t    g_nwaits = 0;

static dd_hold_t g_holds[DD_MAX_MUTEXES];
static size_t    g_nholds = 0;

/* ล็อกคุ้มครองตารางทั้งสอง — static init จึงใช้ได้ทันทีโดยไม่ต้องรอ init() */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/* เตือนเรื่องตารางเต็ม / unlock ไม่ตรงเจ้าของแค่ครั้งเดียว ไม่งั้นท่วมจอจนหาอย่างอื่นไม่เจอ */
static int g_warned_waits = 0;
static int g_warned_holds = 0;
static int g_warned_owner = 0;
static int g_warned_order = 0;
static int g_warned_inv   = 0;

/* ===== ลำดับการล็อก (lock-order inversion) =====
 * ทุกครั้งที่ thread จะ "รอ" mutex ตัวใหม่ขณะถือตัวอื่นอยู่ จดคู่ (ตัวที่ถือ -> ตัวที่ขอ)
 * ถ้าวันหนึ่งเจอคู่กลับทาง = lock-order inversion  ตอนนี้อาจยังไม่ค้าง
 * แต่ถ้าสองลำดับนี้วิ่งพร้อมกันเมื่อไหร่จะ deadlock แบบ bank.c  เป็นการเตือนแบบ prevention
 *
 * จดที่ wait_begin ไม่ใช่ acquired: ตอน deadlock จริง lock ตัวที่สองไม่มีวันได้มา
 * ไม่จด trylock: trylock ไม่มีวันค้าง โค้ดแบบ "ลองแล้วถอย" จึงไม่ใช่ความเสี่ยง
 *
 * ตารางนี้โตตามจำนวน "คู่" ที่เคยเห็นตลอดอายุโปรแกรม ไม่ใช่สถานะ ณ ตอนนี้แบบ waits/holds
 * จึงใช้ hash แบบ open addressing (ยังเป็น static array ห้าม malloc เหมือนเดิม)
 * ไม่ลบคู่ทิ้งเลย จึงไม่ต้องจัดการ tombstone
 *
 * ในนี้พิมพ์รายงานไม่ได้ (ต้องเรียก addr2line ซึ่งช้าและ malloc) จึงฝากเข้าคิว g_inv
 * ให้ detector thread มาหยิบไปพิมพ์ (dd_state_take_inversions)
 */
_Static_assert((DD_MAX_ORDER_PAIRS & (DD_MAX_ORDER_PAIRS - 1)) == 0,
               "DD_MAX_ORDER_PAIRS ต้องเป็นกำลังของ 2");

/* ใส่ได้แค่ 3/4 ของตาราง ไม่งั้น linear probing ยาวขึ้นเรื่อย ๆ ใน lock path */
#define DD_ORDER_LIMIT (DD_MAX_ORDER_PAIRS / 4 * 3)

typedef struct {
    dd_order_t ord;
    int        used;
} order_slot_t;

static order_slot_t   g_order[DD_MAX_ORDER_PAIRS];
static size_t         g_norder = 0;
static dd_inversion_t g_inv[DD_MAX_INVERSIONS];
static size_t         g_ninv = 0;

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

/* ---- ตารางคู่ลำดับ (ต้องถือ g_lock อยู่) ---- */

static size_t order_hash(dd_mutex_t a, dd_mutex_t b) {
    /* ที่อยู่ของ mutex เรียงกัน 8 byte ขึ้นไป บิตล่างเป็นศูนย์ ตัดทิ้งก่อนผสม */
    uintptr_t x = ((uintptr_t)a >> 3) * 0x9E3779B1u ^ ((uintptr_t)b >> 3);
    return (size_t)(x ^ (x >> 16)) & (DD_MAX_ORDER_PAIRS - 1);
}

/* คืนช่องของคู่ (a, b) ถ้ามีแล้ว หรือช่องว่างช่องแรกที่ควรใส่ */
static size_t order_find(dd_mutex_t a, dd_mutex_t b) {
    size_t i = order_hash(a, b);
    for (size_t k = 0; k < DD_MAX_ORDER_PAIRS; k++) {
        if (!g_order[i].used) return i;
        if (g_order[i].ord.first == a && g_order[i].ord.second == b) return i;
        i = (i + 1) & (DD_MAX_ORDER_PAIRS - 1);
    }
    return DD_MAX_ORDER_PAIRS;   /* เต็มทั้งตาราง — ไม่เกิดเพราะหยุดใส่ที่ DD_ORDER_LIMIT */
}

/* t ถือ h อยู่และกำลังจะรอ m — จดคู่ (h -> m) ถ้าเพิ่งเห็นครั้งแรก
 * แล้วเช็คว่าเคยมีคู่กลับทาง (m -> h) หรือยัง  คู่ใหม่ใส่ได้ครั้งเดียว คำเตือนจึงไม่ซ้ำ
 */
static void note_order(dd_tid_t t, const dd_hold_t *h, dd_mutex_t m, dd_site_t site) {
    size_t i = order_find(h->mutex, m);
    if (i == DD_MAX_ORDER_PAIRS || g_order[i].used) return;   /* เคยเห็นคู่นี้แล้ว */
    if (g_norder >= DD_ORDER_LIMIT) {
        if (!g_warned_order) {
            g_warned_order = 1;
            dd_logf("[dd] WARNING: ตารางลำดับการล็อกเต็ม (%d คู่) — คู่ใหม่จะไม่ถูกตรวจ "
                    "lock-order inversion\n", DD_ORDER_LIMIT);
        }
        return;
    }
    g_order[i].used = 1;
    g_order[i].ord.tid         = t;
    g_order[i].ord.first       = h->mutex;
    g_order[i].ord.second      = m;
    g_order[i].ord.first_site  = h->site;
    g_order[i].ord.second_site = site;
    g_norder++;

    size_t j = order_find(m, h->mutex);
    if (j == DD_MAX_ORDER_PAIRS || !g_order[j].used) return;   /* ลำดับยังไม่ขัดกัน */

    if (g_ninv >= DD_MAX_INVERSIONS) {
        if (!g_warned_inv) {
            g_warned_inv = 1;
            dd_logf("[dd] WARNING: คิวคำเตือน lock-order inversion เต็ม (%d) — "
                    "บางคู่จะไม่ถูกรายงาน\n", DD_MAX_INVERSIONS);
        }
        return;
    }
    g_inv[g_ninv].seen    = g_order[j].ord;
    g_inv[g_ninv].reverse = g_order[i].ord;
    g_ninv++;
    dd_dbgf("order: tid %lu ถือ %p แล้วขอ %p สวนกับ tid %lu — inversion\n",
            t, h->mutex, m, g_order[j].ord.tid);
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

    /* ทุกตัวที่ t ถืออยู่ตอนนี้ "มาก่อน" m — ข้ามตัวเดียวกัน (ล็อกซ้ำ ไม่ใช่ลำดับ) */
    for (size_t k = 0; k < g_nholds; k++)
        if (g_holds[k].holder == t && g_holds[k].mutex != m)
            note_order(t, &g_holds[k], m, site);

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
    if (DD_TRACING()) dd_trace_lock_event("wait", t, m, site.pc);

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
        if (DD_TRACING()) dd_trace_lock_event("unwait", t, m, NULL);
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
    if (DD_TRACING()) dd_trace_lock_event("acq", t, m, site.pc);
    dd_state_unlock();
}

void dd_state_released(dd_tid_t t, dd_mutex_t m) {
    dd_state_lock();
    size_t i = find_hold(m);
    if (i < g_nholds) {
        if (g_holds[i].holder != t) {
            /* คนที่ไม่ใช่เจ้าของสั่ง unlock: ERRORCHECK/ROBUST mutex จะคืน EPERM
             * และเจ้าของจริงยังถือ lock อยู่ — ถ้าเราลบ entry ทิ้ง ตารางจะเข้าใจผิดว่าว่าง
             * แล้วพลาด cycle ที่ผ่าน mutex ตัวนี้  จึงคง entry ไว้
             * (NORMAL mutex กรณีนี้เป็น undefined behavior ของโปรแกรมเป้าหมายอยู่แล้ว)
             *
             * อีกสาเหตุที่พบบ่อยกว่ามาก: pthread_cond_wait ปล่อยและล็อก mutex คืนผ่าน
             * ฟังก์ชันภายในของ glibc ที่ไม่ผ่าน wrapper ของเรา ตารางจึงยังจดคนที่เข้า
             * cond_wait ล่าสุดเป็นเจ้าของ  พอคนที่ตื่นขึ้นมา unlock ก็มาตกกรณีนี้
             * โปรแกรม condvar ธรรมดาจึงเจอได้หลายพันครั้ง -> เตือนครั้งเดียว ที่เหลือเป็น debug log
             */
            if (DD_TRACING()) dd_trace_relx(t, m, g_holds[i].holder);
            if (!g_warned_owner) {
                g_warned_owner = 1;
                dd_logf("[dd] WARNING: tid %lu ปลด mutex %p ที่ตารางจดว่า tid %lu ถืออยู่ "
                        "— คงข้อมูลเจ้าของเดิมไว้\n", t, m, g_holds[i].holder);
                dd_logf("[dd]          สาเหตุที่พบบ่อย: pthread_cond_wait (ยังไม่รองรับ) "
                        "หรือ unlock โดยคนที่ไม่ใช่เจ้าของ\n");
                dd_logf("[dd]          เตือนครั้งเดียว ครั้งถัดไปดูได้ด้วย DD_VERBOSE=1\n");
            } else {
                dd_dbgf("WARNING: tid %lu ปลด mutex %p ที่ตารางจดว่า tid %lu ถืออยู่\n",
                        t, m, g_holds[i].holder);
            }
        } else {
            g_holds[i] = g_holds[g_nholds - 1];
            g_nholds--;
            if (DD_TRACING()) dd_trace_lock_event("rel", t, m, NULL);
        }
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

size_t dd_state_take_inversions(dd_inversion_t *out, size_t max) {
    size_t n;

    dd_state_lock();
    n = (g_ninv < max) ? g_ninv : max;
    for (size_t i = 0; i < n; i++)
        out[i] = g_inv[i];
    for (size_t i = n; i < g_ninv; i++)    /* ที่เหลือ (ถ้า max เล็กกว่าคิว) เลื่อนขึ้นมาข้างหน้า */
        g_inv[i - n] = g_inv[i];
    g_ninv -= n;
    dd_state_unlock();
    return n;
}
