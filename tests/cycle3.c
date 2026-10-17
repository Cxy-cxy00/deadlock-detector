/* cycle3.c — deadlock 3 thread วนเป็นวง: T1 รอ T2 รอ T3 รอ T1
 *
 * พิสูจน์ว่า DFS จับ cycle ที่ยาวกว่า 2 ได้ ไม่ได้แค่จับคู่สลับกัน
 *
 *   t1: lock(a) -> lock(b)
 *   t2: lock(b) -> lock(c)
 *   t3: lock(c) -> lock(a)
 *
 * barrier (count = 3) บังคับให้ทั้งสาม thread ถือ lock ตัวแรกครบก่อนไปขอตัวที่สอง
 * จึงค้างเป็นวงยาว 3 ทุกครั้ง ไม่พึ่งจังหวะ sleep
 * แยกเป็นสามฟังก์ชันเพื่อให้รายงานชี้บรรทัดคนละชุด อ่านเทียบกับซอร์สได้ง่าย
 */
#define _DEFAULT_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t a = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t b = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t c = PTHREAD_MUTEX_INITIALIZER;
static pthread_barrier_t all_hold_first;

/* pthread_* คืน error number โดยตรง ไม่ได้ตั้ง errno */
static void die(const char *what, int err) {
    fprintf(stderr, "cycle3: %s: %s\n", what, strerror(err));
    exit(2);
}

static void wait_all_hold_first(void) {
    int rc = pthread_barrier_wait(&all_hold_first);
    /* thread หนึ่งตัวจะได้ PTHREAD_BARRIER_SERIAL_THREAD แทน 0 — ไม่ใช่ error */
    if (rc != 0 && rc != PTHREAD_BARRIER_SERIAL_THREAD)
        die("pthread_barrier_wait", rc);
}

static void *t1(void *x) {
    (void)x;
    pthread_mutex_lock(&a);                 /* t1 ถือ a */
    wait_all_hold_first();
    printf("[t1] รอ b\n"); fflush(stdout);
    pthread_mutex_lock(&b);                 /* t1 รอ b (t2 ถือ) */
    return NULL;
}

static void *t2(void *x) {
    (void)x;
    pthread_mutex_lock(&b);                 /* t2 ถือ b */
    wait_all_hold_first();
    printf("[t2] รอ c\n"); fflush(stdout);
    pthread_mutex_lock(&c);                 /* t2 รอ c (t3 ถือ) */
    return NULL;
}

static void *t3(void *x) {
    (void)x;
    pthread_mutex_lock(&c);                 /* t3 ถือ c */
    wait_all_hold_first();
    printf("[t3] รอ a\n"); fflush(stdout);
    pthread_mutex_lock(&a);                 /* t3 รอ a (t1 ถือ) -> ปิดวง */
    return NULL;
}

int main(void) {
    void *(*fn[3])(void *) = { t1, t2, t3 };
    pthread_t t[3];
    int rc;

    printf("cycle3: deadlock 3 thread วนเป็นวง a -> b -> c -> a\n\n");
    fflush(stdout);

    rc = pthread_barrier_init(&all_hold_first, NULL, 3);
    if (rc != 0) die("pthread_barrier_init", rc);

    for (int i = 0; i < 3; i++) {
        rc = pthread_create(&t[i], NULL, fn[i], NULL);
        if (rc != 0) die("pthread_create", rc);
    }
    for (int i = 0; i < 3; i++) {
        rc = pthread_join(t[i], NULL);      /* ไม่เคยคืน — ทุก thread ค้าง */
        if (rc != 0) die("pthread_join", rc);
    }
    return 0;
}
