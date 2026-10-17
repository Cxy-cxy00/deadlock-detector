/* bench.c — วัด overhead ของ libdetect.so ต่อหนึ่งคู่ lock+unlock
 *
 *   ./build/bench uncontended    1 thread  lock+unlock mutex ตัวเดียว 1,000,000 คู่
 *   ./build/bench contended      4 thread  แย่ง mutex ตัวเดียวกัน รวม 1,000,000 คู่
 *
 * รันแบบมี/ไม่มี LD_PRELOAD แล้วเทียบกัน (tests/run_bench.sh ทำให้)
 * วอร์มอัพ 1 รอบ (ไม่นับ) แล้ววัด 15 รอบ รายงาน median เป็น ns ต่อคู่
 * ใช้ median ไม่ใช่ค่าเฉลี่ย เพราะรอบที่โดน scheduler ขัดจังหวะจะดึงค่าเฉลี่ยเพี้ยน
 *
 * ใช้ NORMAL mutex (ค่าเริ่มต้น) ตาม scope ที่ detector รองรับ
 */
#define _DEFAULT_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PAIRS    1000000L
#define ROUNDS   15
#define NTHREADS 4

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static pthread_barrier_t start_line;

/* pthread_* คืน error number โดยตรง ไม่ได้ตั้ง errno */
static void die(const char *what, int err) {
    fprintf(stderr, "bench: %s: %s\n", what, strerror(err));
    exit(2);
}

static double now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("bench: clock_gettime");
        exit(2);
    }
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static void lock_unlock_loop(long n) {
    for (long i = 0; i < n; i++) {
        int rc = pthread_mutex_lock(&m);
        if (rc != 0) die("pthread_mutex_lock", rc);
        rc = pthread_mutex_unlock(&m);
        if (rc != 0) die("pthread_mutex_unlock", rc);
    }
}

static void *worker(void *arg) {
    long n = *(const long *)arg;
    int rc = pthread_barrier_wait(&start_line);
    if (rc != 0 && rc != PTHREAD_BARRIER_SERIAL_THREAD) die("pthread_barrier_wait", rc);
    lock_unlock_loop(n);
    return NULL;
}

/* คืนเวลารวมเป็น ns ของหนึ่งรอบ (นับเฉพาะช่วงที่ทุก thread เริ่มพร้อมกันจนจบ) */
static double one_round(int contended) {
    if (!contended) {
        double t0 = now_ns();
        lock_unlock_loop(PAIRS);
        return now_ns() - t0;
    }

    pthread_t t[NTHREADS];
    long per = PAIRS / NTHREADS;
    int rc = pthread_barrier_init(&start_line, NULL, NTHREADS + 1);
    if (rc != 0) die("pthread_barrier_init", rc);
    for (int i = 0; i < NTHREADS; i++) {
        rc = pthread_create(&t[i], NULL, worker, &per);
        if (rc != 0) die("pthread_create", rc);
    }
    /* main รอที่ barrier ด้วย จะได้เริ่มจับเวลาหลังสร้าง thread ครบ ไม่นับเวลาสร้าง */
    rc = pthread_barrier_wait(&start_line);
    if (rc != 0 && rc != PTHREAD_BARRIER_SERIAL_THREAD) die("pthread_barrier_wait", rc);
    double t0 = now_ns();
    for (int i = 0; i < NTHREADS; i++) {
        rc = pthread_join(t[i], NULL);
        if (rc != 0) die("pthread_join", rc);
    }
    double dt = now_ns() - t0;
    rc = pthread_barrier_destroy(&start_line);
    if (rc != 0) die("pthread_barrier_destroy", rc);
    return dt;
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv) {
    if (argc != 2 || (strcmp(argv[1], "uncontended") != 0 && strcmp(argv[1], "contended") != 0)) {
        fprintf(stderr, "usage: %s uncontended|contended\n", argv[0]);
        return 2;
    }
    int contended = strcmp(argv[1], "contended") == 0;
    double per_pair[ROUNDS];

    (void)one_round(contended);                       /* วอร์มอัพ ไม่นับ */
    for (int r = 0; r < ROUNDS; r++)
        per_pair[r] = one_round(contended) / (double)PAIRS;

    qsort(per_pair, ROUNDS, sizeof per_pair[0], cmp_double);
    printf("%-11s threads=%d pairs=%ld rounds=%d  median=%.1f ns/pair  min=%.1f  max=%.1f\n",
           argv[1], contended ? NTHREADS : 1, PAIRS, ROUNDS,
           per_pair[ROUNDS / 2], per_pair[0], per_pair[ROUNDS - 1]);
    return 0;
}
