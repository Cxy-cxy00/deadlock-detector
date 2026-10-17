/* no_deadlock.c — เวอร์ชันที่แก้แล้ว ใช้ยืนยันว่าเครื่องมือไม่รายงานผิด (false positive)
 * (ผู้รับผิดชอบ: คนที่ 5)
 *
 * โปรแกรมนี้ใช้บัญชีเดียวกับ bank.c และโอนสวนทางกันเหมือนกัน
 *
 * ต่างกันจุดเดียว: **ล็อกตามลำดับสากลเสมอ**
 *
 *   bank.c          t1: lock(alice) -> lock(bob)     ลำดับสวนกัน = ค้าง
 *                   t2: lock(bob)   -> lock(alice)
 *
 *   ไฟล์นี้          ทั้งคู่: lock(id น้อย) -> lock(id มาก)
 *
 * นี่คือ "deadlock prevention" ด้วยการทำลายเงื่อนไข circular wait ของ Coffman
 * ถ้าทุก thread ขอทรัพยากรตามลำดับเดียวกัน วงจะปิดไม่ได้ตั้งแต่แรก
 *
 * ใช้ทดสอบสองอย่างพร้อมกัน:
 *   1. lock ordering แก้ deadlock ได้จริง (รันแล้วต้องจบ ไม่ค้าง)
 *   2. เครื่องมือของเราไม่เตือนมั่ว (รันผ่าน LD_PRELOAD แล้วต้องเงียบสนิท)
 *
 * ทำไมไม่ใส่ pthread_barrier เหมือน bank.c:
 *   ที่นี่ทั้งสอง thread ขอ mutex ตัวแรก "ตัวเดียวกัน" (alice) ถ้าใส่ barrier หลัง lock ตัวแรก
 *   คนที่ได้ alice จะรอ barrier ส่วนอีกคนรอ alice -> ไม่มีใครถึง barrier ครบสองคน = ค้าง
 *
 * ทำไมต้องวนหลายรอบ:
 *   detector ตื่นมาตรวจทุก DD_CHECK_INTERVAL_MS (500 ms, src/common.h)
 *   ถ้าโปรแกรมจบเร็วกว่าคาบเดียว "ไม่มีรายงาน" ก็ไม่ได้พิสูจน์อะไร เพราะ detector อาจยังไม่ได้ตรวจเลย
 *   จึงวนโอนให้ใช้เวลาอย่างน้อย 3 คาบ และเช็คเวลาจริงตอนจบด้วย
 */
/* -std=c11 ใน Makefile ทำให้ glibc ซ่อน usleep/clock_gettime ไว้ (เหตุผลเดียวกับใน bank.c) */
#define _DEFAULT_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#define HOLD_US 100000   /* 100 ms — ถือ lock ตัวแรกไว้นานเท่า bank.c เคยหน่วง */
#define ROUNDS  10       /* ต่อ thread  ทุกการโอนต้องผ่าน alice ก่อน จึงต่อคิวกัน -> >= 20 x 100 ms */

/* 3 x DD_CHECK_INTERVAL_MS (ถ้าแก้ค่าใน src/common.h ต้องแก้ตรงนี้ด้วย) */
#define MIN_RUN_MS 1500

typedef struct {
    int             id;        /* ลำดับสากลที่ทุก thread ใช้ร่วมกัน */
    const char     *name;
    long            balance;
    pthread_mutex_t lock;
} account_t;

static account_t alice = { 1, "alice", 1000, PTHREAD_MUTEX_INITIALIZER };
static account_t bob   = { 2, "bob",   1000, PTHREAD_MUTEX_INITIALIZER };

/* ---- หัวใจของการแก้ ----
 * ไม่ว่าจะโอนทิศไหน ให้ล็อกบัญชีที่ id น้อยกว่าก่อนเสมอ
 * ใช้ field id แทนการเทียบ pointer เพราะการเทียบ < ระหว่าง pointer ของ
 * object คนละตัวเป็น undefined behavior ใน ISO C
 *
 * ผลคือเขียนเป็นฟังก์ชันเดียวใช้ร่วมกันได้ ไม่ต้องแยกสองตัวเหมือน bank.c
 */
static void transfer(account_t *from, account_t *to, long amount) {
    account_t *first  = (from->id < to->id) ? from : to;
    account_t *second = (from->id < to->id) ? to   : from;

    pthread_mutex_lock(&first->lock);
    usleep(HOLD_US);                    /* ถือ lock ตัวแรกค้างไว้ — ที่นี่ไม่ทำให้ค้าง */
    pthread_mutex_lock(&second->lock);

    from->balance -= amount;
    to->balance   += amount;

    pthread_mutex_unlock(&second->lock);
    pthread_mutex_unlock(&first->lock);
}

static void *t_alice_to_bob(void *arg) {
    (void)arg;
    printf("[t1] โอน alice -> bob %d รอบ\n", ROUNDS);  fflush(stdout);
    for (int i = 0; i < ROUNDS; i++)
        transfer(&alice, &bob, 100);
    printf("[t1] done\n");                             fflush(stdout);
    return NULL;
}

static void *t_bob_to_alice(void *arg) {
    (void)arg;
    printf("[t2] โอน bob -> alice %d รอบ\n", ROUNDS);  fflush(stdout);
    for (int i = 0; i < ROUNDS; i++)
        transfer(&bob, &alice, 200);
    printf("[t2] done\n");                             fflush(stdout);
    return NULL;
}

static int now_ms(long *out) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("clock_gettime");
        return -1;
    }
    *out = ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
    return 0;
}

int main(void) {
    pthread_t t1, t2;
    long total, start, end;

    printf("no_deadlock: บัญชีเดียวกับ bank.c แต่ล็อกตามลำดับสากล (id)\n");
    printf("             ต้องจบเองภายใน ~2 วินาที และเครื่องมือต้องไม่รายงานอะไร\n\n");
    fflush(stdout);

    if (now_ms(&start) != 0) return 1;

    pthread_create(&t1, NULL, t_alice_to_bob, NULL);
    pthread_create(&t2, NULL, t_bob_to_alice, NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    if (now_ms(&end) != 0) return 1;

    total = alice.balance + bob.balance;
    printf("\nยอดคงเหลือ: alice=%ld bob=%ld (รวม %ld)\n",
           alice.balance, bob.balance, total);
    printf("ใช้เวลา %ld ms (ต้องไม่น้อยกว่า %d ms = 3 คาบของ detector)\n",
           end - start, MIN_RUN_MS);

    /* เงินต้องไม่หายไปไหน — ถ้า mutual exclusion เสีย ตัวเลขจะเพี้ยน */
    if (total != 2000) {
        printf("FAIL: ยอดรวมเพี้ยน ควรเป็น 2000\n");
        return 1;
    }
    if (end - start < MIN_RUN_MS) {
        printf("FAIL: จบเร็วเกินไป detector อาจยังตรวจไม่ครบ 3 คาบ\n");
        return 1;
    }
    printf("PASS: จบเองไม่ค้าง ยอดรวมถูกต้อง และรันนานพอให้ detector ตรวจหลายคาบ\n");
    return 0;
}
