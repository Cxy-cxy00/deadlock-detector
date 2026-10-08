/* interpose.c — ดัก pthread_mutex_* ด้วย LD_PRELOAD  (ผู้รับผิดชอบ: คนที่ 1)
 *
 * ===== สถานะ: ต่อ state.c เข้ามาแล้ว (ขั้น 3 รอบที่ 2) =====
 * รอบที่ 1 ทำ pass-through ล้วนก่อน เพื่อพิสูจน์ว่าโปรแกรมเป้าหมายทำงานเหมือนเดิมเป๊ะ
 * รอบที่ 2 เสียบ dd_state_* ตามจุดที่เคยทำเครื่องหมาย TODO ไว้
 * ตารางสถานะจึงมีข้อมูลครบแล้ว รอ graph.c (ขั้น 5) มาอ่านไปสร้าง wait-for graph
 *
 * ===== กลไก =====
 * LD_PRELOAD แทรก .so ของเราไว้หน้าสุดของลำดับค้นหาสัญลักษณ์
 *   ปกติ:  [โปรแกรม] -> [libc.so.6] -> ...
 *   ของเรา: [โปรแกรม] -> [libdetect.so] -> [libc.so.6] -> ...
 * ฟังก์ชันชื่อซ้ำของเราจึงถูกเจอก่อน  แล้วเรียกตัวจริงต่อผ่าน
 * dlsym(RTLD_NEXT, ...) ซึ่งแปลว่า "ตัวถัดไปหลังจากฉันในลำดับนั้น"
 * (ตั้งแต่ glibc 2.34 libpthread ถูกรวมเข้า libc.so.6 แล้ว จึงไปเจอใน libc)
 *
 * ===== ข้อห้ามในไฟล์นี้ =====
 * โค้ดทั้งไฟล์รันอยู่ใน thread ของโปรแกรมคนอื่น จึง:
 *   - ห้าม malloc / printf / ล็อกอะไรก็ตาม
 *   - ภายในไฟล์นี้เรียกได้แต่ real_* เท่านั้น ห้ามเรียก pthread_mutex_* ตรง ๆ
 *   - log ได้เฉพาะผ่าน dd_dbgf (ปิดอยู่ = คืนทันที ไม่มี overhead)
 *
 * หมายเหตุ: ไม่ต้อง #define _GNU_SOURCE ที่นี่ — CFLAGS มี -D_GNU_SOURCE แล้ว (Makefile:4)
 */
#include <dlfcn.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "interpose.h"
#include "state.h"

dd_mutex_fn_t real_mutex_lock    = NULL;
dd_mutex_fn_t real_mutex_trylock = NULL;
dd_mutex_fn_t real_mutex_unlock  = NULL;

/* ธงกัน recursion — ประจำแต่ละ thread
 * ถ้าโค้ดของเราเผลอเรียกอะไรที่ล็อกข้างใน (malloc, dlsym) มันจะวนกลับมาที่
 * pthread_mutex_lock ของเราเอง -> stack overflow  ธงนี้คือเกราะชั้นที่ 1
 * เกราะชั้นที่ 2 คือวินัย: ในไฟล์นี้เรียกแต่ real_* เท่านั้น
 */
static __thread int in_dd = 0;

/* ---- thread id ที่ใช้รายงาน ----
 * ใช้ kernel TID (ไม่ใช่ pthread_self) เพราะเลขตรงกับที่ ps -L / top -H / gdb แสดง
 * เอาไปเทียบกันได้ตอน demo  syscall ไม่มี lock จึงปลอดภัยใน lock path
 * cache ไว้ใน __thread เพราะ TID ของ thread หนึ่งไม่เคยเปลี่ยน -> เสีย syscall แค่ครั้งแรก
 * (ข้อจำกัดที่รู้อยู่: ถ้าโปรแกรมเป้าหมาย fork() ค่าที่ cache ไว้จะเก่า — ไม่รองรับ fork)
 */
static __thread dd_tid_t t_tid = 0;   /* 0 = ยังไม่เคยถาม (TID จริงเริ่มที่ 1) */

dd_tid_t dd_self(void) {
    if (t_tid == 0)
        t_tid = (dd_tid_t)syscall(SYS_gettid);
    return t_tid;
}

dd_site_t dd_site_from_pc(void *pc) {
    dd_site_t s = { NULL, 0, pc };   /* file/line ยังว่าง — ขั้น 9 ค่อย resolve */
    return s;
}

/* ---- หาที่อยู่ของฟังก์ชันตัวจริง ----
 * เรียกจาก constructor (detector.c:17) ซึ่งรันก่อน main ตอนที่ยังมี thread เดียว
 *
 * ระวังไก่กับไข่: dlsym อาจเรียก malloc ข้างใน -> malloc เรียก pthread_mutex_lock
 * -> คือตัวเรา -> เราเรียก dlsym อีก -> วนไม่สิ้นสุด  ธง in_dd ตัดวงนี้
 */
/* ประกาศว่า thread ที่เรียกเป็น thread ภายในของเราเอง (detector)
 * ตั้งธงค้างไว้ถาวร ทุก lock ที่ thread นี้ทำจะผ่านไปตัวจริงโดยไม่ถูกบันทึก
 * ป้องกันไม่ให้ detector โผล่ไปเป็น node ในกราฟที่ตัวเองกำลังตรวจ
 */
void dd_interpose_mark_self_internal(void) { in_dd = 1; }

void dd_interpose_resolve(void) {
    if (in_dd) return;
    in_dd = 1;

    if (real_mutex_lock == NULL)
        real_mutex_lock = (dd_mutex_fn_t)dlsym(RTLD_NEXT, "pthread_mutex_lock");
    if (real_mutex_trylock == NULL)
        real_mutex_trylock = (dd_mutex_fn_t)dlsym(RTLD_NEXT, "pthread_mutex_trylock");
    if (real_mutex_unlock == NULL)
        real_mutex_unlock = (dd_mutex_fn_t)dlsym(RTLD_NEXT, "pthread_mutex_unlock");

    in_dd = 0;
}

/* ==== ฟังก์ชันที่ไปทับของ libc ====
 *
 * ทั้งสามตัวมีโครงเหมือนกัน:
 *   1) ยังไม่ resolve -> resolve ก่อน (เผื่อถูกเรียกก่อน constructor)
 *   2) อยู่ในโค้ดเราเอง (in_dd) -> ผ่านไปตัวจริงเลย ไม่จดอะไร
 *   3) จด -> เรียกตัวจริง -> จด
 *
 * กรณี real_* ยังเป็น NULL หลัง resolve: เกิดได้เฉพาะช่วงบูตที่ dlsym ยังไม่พร้อม
 * ซึ่งแคบมากและเป็น single-thread  เลือก return 0 (ทำเป็นว่าสำเร็จ) เพราะถ้า
 * return error โปรแกรมเป้าหมายจะพังทันที  เป็น wart ที่รู้ตัว — มี dd_dbgf เตือนไว้
 */

int pthread_mutex_lock(pthread_mutex_t *m) {
    void *pc = __builtin_return_address(0);   /* ที่อยู่ในโปรแกรมเป้าหมาย */

    if (real_mutex_lock == NULL) {
        dd_interpose_resolve();
        if (real_mutex_lock == NULL) {
            dd_dbgf("BOOTSTRAP lock %p (ยังไม่ resolve)\n", (void *)m);
            return 0;
        }
    }
    if (in_dd) return real_mutex_lock(m);

    in_dd = 1;
    dd_dbgf("lock    %p tid=%lu pc=%p\n", (void *)m, dd_self(), pc);
    dd_state_wait_begin(dd_self(), m, dd_site_from_pc(pc));
    in_dd = 0;

    /* ---- จุดที่ค้างจริงตอนเกิด deadlock ---- */
    int rc = real_mutex_lock(m);

    in_dd = 1;
    /* ต้องเรียก wait_end แม้ rc != 0 ไม่งั้นมี wait edge ผีค้างในตาราง
     * -> detector จะรายงาน deadlock ที่ไม่มีจริง */
    dd_state_wait_end(dd_self(), m);
    if (rc == 0) dd_state_acquired(dd_self(), m, dd_site_from_pc(pc));
    dd_dbgf("locked  %p tid=%lu rc=%d\n", (void *)m, dd_self(), rc);
    in_dd = 0;

    return rc;
}

int pthread_mutex_trylock(pthread_mutex_t *m) {
    void *pc = __builtin_return_address(0);

    if (real_mutex_trylock == NULL) {
        dd_interpose_resolve();
        if (real_mutex_trylock == NULL) {
            dd_dbgf("BOOTSTRAP trylock %p (ยังไม่ resolve)\n", (void *)m);
            return 0;
        }
    }
    if (in_dd) return real_mutex_trylock(m);

    /* trylock ไม่มีวันค้าง จึงไม่ต้องลง wait edge เลย — บันทึกเฉพาะตอนได้จริง */
    int rc = real_mutex_trylock(m);

    in_dd = 1;
    if (rc == 0) dd_state_acquired(dd_self(), m, dd_site_from_pc(pc));
    dd_dbgf("trylock %p tid=%lu rc=%d pc=%p\n", (void *)m, dd_self(), rc, pc);
    in_dd = 0;

    return rc;
}

int pthread_mutex_unlock(pthread_mutex_t *m) {
    if (real_mutex_unlock == NULL) {
        dd_interpose_resolve();
        if (real_mutex_unlock == NULL) {
            dd_dbgf("BOOTSTRAP unlock %p (ยังไม่ resolve)\n", (void *)m);
            return 0;
        }
    }
    if (in_dd) return real_mutex_unlock(m);

    /* จด "ปล่อยแล้ว" ก่อนปล่อยจริงเสมอ
     * ถ้าปล่อยจริงก่อน thread อื่นอาจคว้า mutex ไปทันทีแล้วจดตัวเองเป็นเจ้าของ
     * พอเราค่อยมาจดทีหลังจะไปลบข้อมูลของเขาทิ้ง
     */
    in_dd = 1;
    dd_state_released(dd_self(), m);
    dd_dbgf("unlock  %p tid=%lu\n", (void *)m, dd_self());
    in_dd = 0;

    return real_mutex_unlock(m);
}
