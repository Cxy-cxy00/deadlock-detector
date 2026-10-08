/* bank.c — โปรแกรมทดสอบตัวหลัก: โอนเงินสองทิศทางพร้อมกันจนเกิด deadlock
 * (ผู้รับผิดชอบ: คนที่ 5)
 *
 * โปรแกรมนี้ "พัง" โดยตั้งใจ — มันคือข้อสอบที่ใช้วัดว่า libdetect.so ทำงานถูกหรือเปล่า
 * อย่าแก้ให้มันไม่ค้าง  ถ้าต้องการเวอร์ชันที่ถูกต้องให้ดู no_deadlock.c
 *
 * ทำไมต้องมี usleep คั่นระหว่างสอง lock:
 *   ถ้าไม่มี thread แรกอาจทำงานจบก่อนที่ thread ที่สองจะได้คิว แล้วไม่ค้างเลย
 *   deadlock จะเกิดแบบสุ่ม ซึ่งใช้ทดสอบไม่ได้ (แยกไม่ออกว่า detector หาไม่เจอ
 *   หรือรอบนี้ไม่มีอะไรให้หา)  usleep บังคับให้ทั้งคู่คว้า lock ตัวแรกได้ก่อน
 *   แล้วไปชนกันที่ตัวที่สองพร้อมกัน จึงค้างทุกครั้ง
 *
 * ทำไมเขียน lock ซ้ำสองที่ ไม่รวมเป็นฟังก์ชัน transfer() ตัวเดียว:
 *   เพื่อให้แต่ละ thread รายงาน "บรรทัดคนละชุด" ตามตัวอย่าง output ใน README
 *   ถ้าใช้ฟังก์ชันร่วมกัน ทั้งสอง thread จะชี้บรรทัดเดียวกัน อ่านแล้วเหมือนเครื่องมือพัง
 *
 * ต้อง compile ด้วย -g -O0 เสมอ (Makefile ตั้งไว้แล้ว) ไม่งั้น detector ชี้บรรทัดไม่ได้
 */
/* -std=c11 ใน Makefile ทำให้ glibc ซ่อน usleep() ไว้ (มันเป็น POSIX ไม่ใช่ C มาตรฐาน)
 * ถ้าไม่เปิด _DEFAULT_SOURCE ก่อน include  gcc 14+ จะ error "implicit declaration of usleep"
 * หมายเหตุ: ฝั่ง src/ ไม่เจอปัญหานี้เพราะ CFLAGS มี -D_GNU_SOURCE อยู่แล้ว (Makefile:6)
 */
#define _DEFAULT_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

/* นานพอให้อีก thread คว้า lock ตัวแรกของมันได้แน่นอน */
#define HOLD_US 100000   /* 100 ms */

typedef struct {
    const char     *name;
    long            balance;
    pthread_mutex_t lock;
} account_t;

static account_t alice = { "alice", 1000, PTHREAD_MUTEX_INITIALIZER };
static account_t bob   = { "bob",   1000, PTHREAD_MUTEX_INITIALIZER };

/* ---- thread 1: โอน alice -> bob   (ล็อก alice ก่อน แล้วค่อย bob) ---- */
static void *transfer_alice_to_bob(void *arg) {
    (void)arg;
    const long amount = 100;

    printf("[t1] lock alice\n");          fflush(stdout);
    pthread_mutex_lock(&alice.lock);      /* <-- รายงานต้องชี้บรรทัดนี้เป็น "holds" */

    usleep(HOLD_US);                      /* เปิดช่องให้ t2 คว้า bob ไปก่อน */

    printf("[t1] lock bob ...\n");        fflush(stdout);
    pthread_mutex_lock(&bob.lock);        /* <-- ค้างตรงนี้ รายงานชี้เป็น "waiting" */

    alice.balance -= amount;
    bob.balance   += amount;

    pthread_mutex_unlock(&bob.lock);
    pthread_mutex_unlock(&alice.lock);
    printf("[t1] done\n");                fflush(stdout);
    return NULL;
}

/* ---- thread 2: โอน bob -> alice   (ล็อก bob ก่อน แล้วค่อย alice) ----
 * ลำดับสวนทางกับ thread 1 — นี่คือต้นเหตุของ deadlock ทั้งหมด
 */
static void *transfer_bob_to_alice(void *arg) {
    (void)arg;
    const long amount = 200;

    printf("[t2] lock bob\n");             fflush(stdout);
    pthread_mutex_lock(&bob.lock);         /* <-- "holds" ของ t2 */

    usleep(HOLD_US);                       /* เปิดช่องให้ t1 คว้า alice ไปก่อน */

    printf("[t2] lock alice ...\n");       fflush(stdout);
    pthread_mutex_lock(&alice.lock);       /* <-- "waiting" ของ t2 ค้างตรงนี้ */

    bob.balance   -= amount;
    alice.balance += amount;

    pthread_mutex_unlock(&alice.lock);
    pthread_mutex_unlock(&bob.lock);
    printf("[t2] done\n");                 fflush(stdout);
    return NULL;
}

int main(void) {
    pthread_t t1, t2;

    printf("bank: โอนเงินสองทิศทางพร้อมกัน\n");
    printf("      เงียบไปเกิน 1 วินาที = deadlock แล้ว (Ctrl-C เพื่อออก)\n\n");
    fflush(stdout);

    pthread_create(&t1, NULL, transfer_alice_to_bob, NULL);
    pthread_create(&t2, NULL, transfer_bob_to_alice, NULL);

    /* ไม่เคยคืนค่า — ทั้งสอง thread ค้างอยู่ใน pthread_mutex_lock */
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    /* บรรทัดนี้จะถูกพิมพ์เฉพาะตอนที่ deadlock "ไม่" เกิด เอาไว้เช็คว่าเราไม่ได้ลืมอะไร */
    printf("\nยอดคงเหลือ: alice=%ld bob=%ld (รวม %ld)\n",
           alice.balance, bob.balance, alice.balance + bob.balance);
    return 0;
}
