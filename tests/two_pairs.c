/* two_pairs.c — deadlock สองวงที่ไม่เกี่ยวกันเกิดพร้อมกัน
 *
 *   วงที่ 1:  t1 ถือ a1 รอ b1    t2 ถือ b1 รอ a1
 *   วงที่ 2:  t3 ถือ a2 รอ b2    t4 ถือ b2 รอ a2
 *
 * ใช้ยืนยันว่า detector รายงาน "ทุกวง" ในการตรวจรอบเดียว ไม่ใช่แค่วงแรกที่ DFS เจอ
 * และรายงานแต่ละวงครั้งเดียว (ไม่พิมพ์ซ้ำทุกคาบ)  ผลที่ต้องได้: [DEADLOCK DETECTED] 2 ครั้ง
 *
 * barrier (count = 4) บังคับให้ทั้ง 4 thread ถือ lock ตัวแรกครบก่อนไปขอตัวที่สอง
 * ทั้งสองวงจึงเกิดทุกครั้ง ไม่พึ่งจังหวะ
 */
#define _DEFAULT_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t a1 = PTHREAD_MUTEX_INITIALIZER, b1 = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t a2 = PTHREAD_MUTEX_INITIALIZER, b2 = PTHREAD_MUTEX_INITIALIZER;
static pthread_barrier_t all_hold_first;

/* pthread_* คืน error number โดยตรง ไม่ได้ตั้ง errno */
static void die(const char *what, int err) {
    fprintf(stderr, "two_pairs: %s: %s\n", what, strerror(err));
    exit(2);
}

typedef struct { const char *name; pthread_mutex_t *first, *second; } job_t;

static void *run(void *arg) {
    const job_t *j = arg;
    pthread_mutex_lock(j->first);

    int rc = pthread_barrier_wait(&all_hold_first);
    if (rc != 0 && rc != PTHREAD_BARRIER_SERIAL_THREAD) die("pthread_barrier_wait", rc);

    printf("[%s] รอ lock ตัวที่สอง ...\n", j->name);  fflush(stdout);
    pthread_mutex_lock(j->second);                    /* ค้างตรงนี้ทั้ง 4 thread */

    pthread_mutex_unlock(j->second);
    pthread_mutex_unlock(j->first);
    return NULL;
}

int main(void) {
    static const job_t jobs[4] = {
        { "t1", &a1, &b1 }, { "t2", &b1, &a1 },     /* วงที่ 1 */
        { "t3", &a2, &b2 }, { "t4", &b2, &a2 },     /* วงที่ 2 */
    };
    pthread_t t[4];
    int rc;

    printf("two_pairs: deadlock สองวงพร้อมกัน — เครื่องมือต้องรายงานครบทั้งสองวง\n\n");
    fflush(stdout);

    rc = pthread_barrier_init(&all_hold_first, NULL, 4);
    if (rc != 0) die("pthread_barrier_init", rc);

    for (int i = 0; i < 4; i++) {
        rc = pthread_create(&t[i], NULL, run, (void *)&jobs[i]);
        if (rc != 0) die("pthread_create", rc);
    }
    for (int i = 0; i < 4; i++) {
        rc = pthread_join(t[i], NULL);          /* ไม่เคยคืน — ทุก thread ค้าง */
        if (rc != 0) die("pthread_join", rc);
    }
    return 0;
}
