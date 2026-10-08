/* no_deadlock.c — เวอร์ชันที่แก้แล้ว ใช้ยืนยันว่าเครื่องมือไม่รายงานผิด (false positive)
 * (ผู้รับผิดชอบ: คนที่ 5)
 *
 * โปรแกรมนี้ทำงานเหมือน bank.c ทุกอย่าง — บัญชีเดียวกัน จำนวนเงินเดียวกัน
 * โอนสวนทางกันเหมือนกัน และหน่วงเวลาเท่ากันเป๊ะ
 *
 * ต่างกันจุดเดียว: **ล็อกตามลำดับสากลเสมอ**
 *
 *   bank.c          t1: lock(alice) -> lock(bob)     ลำดับสวนกัน = ค้าง
 *                   t2: lock(bob)   -> lock(alice)
 *
 *   ไฟล์นี้          ทั้งคู่: lock(ตัวที่อยู่ก่อน) -> lock(ตัวที่อยู่หลัง)
 *
 * นี่คือ "deadlock prevention" ด้วยการทำลายเงื่อนไข circular wait ของ Coffman
 * ถ้าทุก thread ขอทรัพยากรตามลำดับเดียวกัน วงจะปิดไม่ได้ตั้งแต่แรก
 *
 * ใช้ทดสอบสองอย่างพร้อมกัน:
 *   1. lock ordering แก้ deadlock ได้จริง (รันแล้วต้องจบ ไม่ค้าง)
 *   2. เครื่องมือของเราไม่เตือนมั่ว (รันผ่าน LD_PRELOAD แล้วต้องเงียบสนิท)
 */
/* -std=c11 ใน Makefile ทำให้ glibc ซ่อน usleep() ไว้ (เหตุผลเดียวกับใน bank.c) */
#define _DEFAULT_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#define HOLD_US 100000   /* 100 ms — เท่ากับ bank.c เป๊ะ เพื่อตัดเรื่องจังหวะออกไป */

typedef struct {
    const char     *name;
    long            balance;
    pthread_mutex_t lock;
} account_t;

static account_t alice = { "alice", 1000, PTHREAD_MUTEX_INITIALIZER };
static account_t bob   = { "bob",   1000, PTHREAD_MUTEX_INITIALIZER };

/* ---- หัวใจของการแก้ ----
 * ไม่ว่าจะโอนทิศไหน ให้ล็อกบัญชีที่ "มาก่อน" ในลำดับสากลเสมอ
 * ที่นี่ใช้ที่อยู่ของตัวแปรเป็นตัวเรียง จะใช้เลขบัญชีหรือชื่อก็ได้
 * ขอแค่ทุก thread ตกลงใช้ลำดับเดียวกัน
 *
 * ผลคือเขียนเป็นฟังก์ชันเดียวใช้ร่วมกันได้ ไม่ต้องแยกสองตัวเหมือน bank.c
 */
static void transfer(account_t *from, account_t *to, long amount) {
    account_t *first  = (from < to) ? from : to;
    account_t *second = (from < to) ? to   : from;

    pthread_mutex_lock(&first->lock);
    usleep(HOLD_US);                    /* หน่วงเท่า bank.c — ที่นี่ไม่ทำให้ค้าง */
    pthread_mutex_lock(&second->lock);

    from->balance -= amount;
    to->balance   += amount;

    pthread_mutex_unlock(&second->lock);
    pthread_mutex_unlock(&first->lock);
}

static void *t_alice_to_bob(void *arg) {
    (void)arg;
    printf("[t1] โอน alice -> bob\n");  fflush(stdout);
    transfer(&alice, &bob, 100);
    printf("[t1] done\n");              fflush(stdout);
    return NULL;
}

static void *t_bob_to_alice(void *arg) {
    (void)arg;
    printf("[t2] โอน bob -> alice\n");  fflush(stdout);
    transfer(&bob, &alice, 200);
    printf("[t2] done\n");              fflush(stdout);
    return NULL;
}

int main(void) {
    pthread_t t1, t2;
    long total;

    printf("no_deadlock: โค้ดเดียวกับ bank.c แต่ล็อกตามลำดับสากล\n");
    printf("             ต้องจบเองภายใน ~1 วินาที และเครื่องมือต้องไม่รายงานอะไร\n\n");
    fflush(stdout);

    pthread_create(&t1, NULL, t_alice_to_bob, NULL);
    pthread_create(&t2, NULL, t_bob_to_alice, NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    total = alice.balance + bob.balance;
    printf("\nยอดคงเหลือ: alice=%ld bob=%ld (รวม %ld)\n",
           alice.balance, bob.balance, total);

    /* เงินต้องไม่หายไปไหน — ถ้า mutual exclusion เสีย ตัวเลขจะเพี้ยน */
    if (total != 2000) {
        printf("FAIL: ยอดรวมเพี้ยน ควรเป็น 2000\n");
        return 1;
    }
    printf("PASS: จบเองไม่ค้าง และยอดรวมถูกต้อง\n");
    return 0;
}
