/* errcheck.c — regression test ของข้อ 2.4: unlock โดยคนที่ไม่ใช่เจ้าของต้องไม่ลบเจ้าของเดิม
 *
 *   1. t1 ล็อก E (ERRORCHECK)
 *   2. t2 สั่ง unlock(E) ทั้งที่ไม่ใช่เจ้าของ -> EPERM  t1 ยังถือ E อยู่จริง
 *   3. t1 กับ t2 deadlock กันผ่าน E:
 *        t1: ถือ E -> รอ B
 *        t2: ถือ B -> รอ E
 *
 * ถ้า state.c ลบ entry ตอน unlock ข้อ 2 ตารางจะคิดว่า E ว่าง
 * edge t2 -> t1 หายไป แล้วไม่มีรายงาน
 * ต้องได้: ค้าง (exit 124) + [DEADLOCK DETECTED] 2 threads in cycle + WARNING 1 ครั้ง
 */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t E;                                  /* ERRORCHECK — init ใน main */
static pthread_mutex_t B = PTHREAD_MUTEX_INITIALIZER;
static pthread_barrier_t step;                             /* ใช้สองครั้ง: ก่อน unlock ผิด และก่อนขอตัวที่สอง */

/* pthread_* คืน error number โดยตรง ไม่ได้ตั้ง errno */
static void die(const char *what, int err) {
    fprintf(stderr, "errcheck: %s: %s\n", what, strerror(err));
    exit(2);
}

static void wait_step(void) {
    int rc = pthread_barrier_wait(&step);
    if (rc != 0 && rc != PTHREAD_BARRIER_SERIAL_THREAD)
        die("pthread_barrier_wait", rc);
}

static void *t1(void *x) {
    (void)x;
    pthread_mutex_lock(&E);                /* t1 ถือ E */
    wait_step();                           /* ให้ t2 ลอง unlock E */
    wait_step();                           /* รอ t2 ถือ B */
    printf("[t1] รอ B\n"); fflush(stdout);
    pthread_mutex_lock(&B);                /* ค้าง: t2 ถือ B */
    return NULL;
}

static void *t2(void *x) {
    (void)x;
    wait_step();                           /* t1 ถือ E แล้วแน่ ๆ */
    int rc = pthread_mutex_unlock(&E);     /* ไม่ใช่เจ้าของ */
    printf("[t2] unlock E ที่ไม่ได้ถือ -> rc=%d (%s)\n", rc, rc == EPERM ? "EPERM" : strerror(rc));
    fflush(stdout);
    if (rc != EPERM) {
        printf("FAIL: ควรได้ EPERM\n");
        exit(1);
    }
    pthread_mutex_lock(&B);                /* t2 ถือ B */
    wait_step();
    printf("[t2] รอ E\n"); fflush(stdout);
    pthread_mutex_lock(&E);                /* ค้าง: t1 ยังถือ E */
    return NULL;
}

int main(void) {
    pthread_mutexattr_t attr;
    pthread_t a, b;
    int rc;

    pthread_mutexattr_init(&attr);
    rc = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    if (rc != 0) die("settype", rc);
    rc = pthread_mutex_init(&E, &attr);
    if (rc != 0) die("mutex_init", rc);
    pthread_mutexattr_destroy(&attr);

    rc = pthread_barrier_init(&step, NULL, 2);
    if (rc != 0) die("pthread_barrier_init", rc);

    pthread_create(&a, NULL, t1, NULL);
    pthread_create(&b, NULL, t2, NULL);
    pthread_join(a, NULL);
    pthread_join(b, NULL);

    printf("FAIL: ไม่ควรมาถึงบรรทัดนี้\n");
    return 1;
}
