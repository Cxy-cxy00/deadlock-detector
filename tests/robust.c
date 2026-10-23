/* robust.c — regression test ของข้อ 2.3: robust mutex ที่คืน EOWNERDEAD ต้องนับว่าได้ lock
 *
 *   1. thread "ghost" ล็อก R แล้วจบไปทั้งที่ยังถืออยู่
 *   2. main ล็อก R -> ได้ EOWNERDEAD (= ได้ lock แล้ว แต่เจ้าของเดิมตาย)
 *   3. main กับ t2 deadlock กันผ่าน R:
 *        main: ถือ R -> รอ B
 *        t2  : ถือ B -> รอ R
 *
 * ถ้า interpose.c จดเฉพาะ rc == 0 ตารางจะยังคิดว่า ghost (ที่ตายแล้ว) ถือ R
 * edge ของ t2 จะชี้ไปหา ghost แทน main แล้ววงหายไป -> ไม่มีรายงาน
 * ต้องได้: ค้าง (exit 124) + [DEADLOCK DETECTED] 2 threads in cycle
 */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t R;                                  /* robust — init ใน main */
static pthread_mutex_t B = PTHREAD_MUTEX_INITIALIZER;
static pthread_barrier_t both_hold_first;

/* pthread_* คืน error number โดยตรง ไม่ได้ตั้ง errno */
static void die(const char *what, int err) {
    fprintf(stderr, "robust: %s: %s\n", what, strerror(err));
    exit(2);
}

static void wait_both_hold_first(void) {
    int rc = pthread_barrier_wait(&both_hold_first);
    if (rc != 0 && rc != PTHREAD_BARRIER_SERIAL_THREAD)
        die("pthread_barrier_wait", rc);
}

static void *ghost(void *x) {
    (void)x;
    int rc = pthread_mutex_lock(&R);
    if (rc != 0) die("ghost lock R", rc);
    return NULL;                           /* จบโดยไม่ unlock — ตั้งใจ */
}

static void *t2(void *x) {
    (void)x;
    pthread_mutex_lock(&B);                /* t2 ถือ B */
    wait_both_hold_first();
    printf("[t2] รอ R\n"); fflush(stdout);
    pthread_mutex_lock(&R);                /* ค้าง: main ถือ R */
    return NULL;
}

int main(void) {
    pthread_mutexattr_t attr;
    pthread_t g, t;
    int rc;

    pthread_mutexattr_init(&attr);
    rc = pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
    if (rc != 0) die("setrobust", rc);
    rc = pthread_mutex_init(&R, &attr);
    if (rc != 0) die("mutex_init", rc);
    pthread_mutexattr_destroy(&attr);

    rc = pthread_barrier_init(&both_hold_first, NULL, 2);
    if (rc != 0) die("pthread_barrier_init", rc);

    pthread_create(&g, NULL, ghost, NULL);
    pthread_join(g, NULL);

    rc = pthread_mutex_lock(&R);           /* main ถือ R */
    printf("[main] lock R -> rc=%d (%s)\n", rc, rc == EOWNERDEAD ? "EOWNERDEAD" : strerror(rc));
    fflush(stdout);
    if (rc != EOWNERDEAD) {
        printf("FAIL: ควรได้ EOWNERDEAD\n");
        return 1;
    }
    pthread_mutex_consistent(&R);

    pthread_create(&t, NULL, t2, NULL);
    wait_both_hold_first();
    printf("[main] รอ B\n"); fflush(stdout);
    pthread_mutex_lock(&B);                /* ค้าง: t2 ถือ B */

    printf("FAIL: ไม่ควรมาถึงบรรทัดนี้\n");
    return 1;
}
