/* longcycle.c — regression test ของข้อ 2.7: cycle ยาวเกิน DD_MAX_CYCLE_LEN (64)
 *
 * N = 70 thread  thread i ถือ m[i] แล้วรอ m[(i+1) % N]  วนเป็นวงยาว 70
 * barrier (count = N) บังคับให้ทุกตัวถือ lock ตัวแรกครบก่อน จึงค้างเป็นวงเต็มทุกครั้ง
 *
 * ต้องได้: ค้าง (exit 124) + "70 threads in cycle (partial path, truncated: แสดง 64)"
 * และบรรทัด Cycle ต้องจบด้วย "... (partial path, truncated)" ไม่ปิดวงกลับ thread แรก
 *
 * หมายเหตุ: รายงานเรียก addr2line ทีละบรรทัด (~128 ครั้ง) จึงใช้เวลาหลายวินาที
 * ตั้ง timeout ให้เผื่อไว้ (run_regress.sh ใช้ 20 วินาที)
 */
#define _DEFAULT_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N 70

static pthread_mutex_t m[N];
static pthread_barrier_t all_hold_first;

/* pthread_* คืน error number โดยตรง ไม่ได้ตั้ง errno */
static void die(const char *what, int err) {
    fprintf(stderr, "longcycle: %s: %s\n", what, strerror(err));
    exit(2);
}

static void *worker(void *arg) {
    long i = (long)arg;
    pthread_mutex_lock(&m[i]);             /* ถือ m[i] */
    int rc = pthread_barrier_wait(&all_hold_first);
    if (rc != 0 && rc != PTHREAD_BARRIER_SERIAL_THREAD)
        die("pthread_barrier_wait", rc);
    pthread_mutex_lock(&m[(i + 1) % N]);   /* ค้าง: thread ถัดไปถืออยู่ */
    return NULL;
}

int main(void) {
    pthread_t t[N];
    int rc;

    for (int i = 0; i < N; i++)
        pthread_mutex_init(&m[i], NULL);
    rc = pthread_barrier_init(&all_hold_first, NULL, N);
    if (rc != 0) die("pthread_barrier_init", rc);

    printf("longcycle: %d thread วนเป็นวงเดียว\n", N); fflush(stdout);
    for (long i = 0; i < N; i++) {
        rc = pthread_create(&t[i], NULL, worker, (void *)i);
        if (rc != 0) die("pthread_create", rc);
    }
    for (int i = 0; i < N; i++)
        pthread_join(t[i], NULL);

    printf("FAIL: ไม่ควรมาถึงบรรทัดนี้\n");
    return 1;
}
