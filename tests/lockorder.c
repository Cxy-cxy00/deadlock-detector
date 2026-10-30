/* lockorder.c — lock-order inversion ที่ "ยังไม่ค้าง" (regression test ของการเตือนล่วงหน้า)
 *
 * t1 ล็อก a แล้ว b   จบแล้ว join   จากนั้น t2 ล็อก b แล้ว a
 * สองลำดับนี้ไม่เคยวิ่งพร้อมกัน โปรแกรมจึงจบเองทุกครั้ง
 * แต่ถ้าวันไหนวิ่งพร้อมกัน = bank.c ทันที  เครื่องมือจึงต้องเตือนตั้งแต่ตอนนี้
 *
 * ตรวจด้วย:
 *   LD_PRELOAD=./build/libdetect.so ./build/lockorder 2>err.txt
 *   -> exit 0, [LOCK ORDER INVERSION] 1 ครั้ง, ไม่มี [DEADLOCK DETECTED]
 *
 * ไม่ต้องใช้ barrier หรือ sleep: join คั่นกลางทำให้ลำดับเหตุการณ์แน่นอนอยู่แล้ว
 * โปรแกรมจบเร็วกว่าคาบของ detector — คำเตือนจึงออกตอน library ปิด (destructor ใน detector.c)
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t a = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t b = PTHREAD_MUTEX_INITIALIZER;

/* pthread_* คืน error number โดยตรง ไม่ได้ตั้ง errno */
static void die(const char *what, int err) {
    fprintf(stderr, "lockorder: %s: %s\n", what, strerror(err));
    exit(2);
}

/* ---- ลำดับที่ 1: a ก่อน b ---- */
static void *a_then_b(void *arg) {
    (void)arg;
    pthread_mutex_lock(&a);          /* <-- รายงานต้องชี้บรรทัดนี้เป็น "locked" */
    pthread_mutex_lock(&b);          /* <-- และบรรทัดนี้เป็น "then" */
    printf("[t1] a -> b\n");         fflush(stdout);
    pthread_mutex_unlock(&b);
    pthread_mutex_unlock(&a);
    return NULL;
}

/* ---- ลำดับที่ 2: b ก่อน a — สวนกับข้างบน ---- */
static void *b_then_a(void *arg) {
    (void)arg;
    pthread_mutex_lock(&b);
    pthread_mutex_lock(&a);
    printf("[t2] b -> a\n");         fflush(stdout);
    pthread_mutex_unlock(&a);
    pthread_mutex_unlock(&b);
    return NULL;
}

static void run(void *(*fn)(void *)) {
    pthread_t t;
    int rc;

    if ((rc = pthread_create(&t, NULL, fn, NULL)) != 0) die("pthread_create", rc);
    if ((rc = pthread_join(t, NULL)) != 0)             die("pthread_join", rc);
}

int main(void) {
    printf("lockorder: ล็อก a->b แล้วค่อย b->a คนละช่วงเวลา — ไม่ค้าง แต่ลำดับขัดกัน\n\n");
    fflush(stdout);

    run(a_then_b);
    run(b_then_a);

    printf("\nPASS: จบเองไม่ค้าง (เครื่องมือควรเตือน lock-order inversion ทาง stderr)\n");
    return 0;
}
