/* condvar.c — สอง thread ผลัดกันทำงานด้วย condition variable ตัวเดียว ไม่มี deadlock
 * (regression test ของ warning "ปลด mutex ที่ตารางจดว่า ... ถืออยู่")
 *
 * condition variable ยังไม่รองรับ (ดู Limitations ใน README):
 *   pthread_cond_wait ปล่อยและล็อก mutex คืนผ่านฟังก์ชันภายในของ glibc
 *   ไม่ผ่าน wrapper ของเรา ตารางจึงจดเจ้าของ mutex ผิดคนอยู่เรื่อย ๆ
 * ก่อนแก้ โปรแกรมนี้ทำให้มี WARNING ราว 3,000 บรรทัด  หลังแก้ต้องเหลือครั้งเดียว
 * และต้องไม่มี [DEADLOCK DETECTED] เพราะไม่มี deadlock จริง
 *
 * ตรวจด้วย:
 *   LD_PRELOAD=./build/libdetect.so ./build/condvar 2>err.txt
 *   -> exit 0, grep -c WARNING err.txt = 1, ไม่มี DEADLOCK
 */
#define _DEFAULT_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROUNDS 2000

static pthread_mutex_t m  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cv = PTHREAD_COND_INITIALIZER;
static int turn = 0;          /* ถึงตาใคร: 0 หรือ 1 */
static int played[2];

/* pthread_* คืน error number โดยตรง ไม่ได้ตั้ง errno */
static void die(const char *what, int err) {
    fprintf(stderr, "condvar: %s: %s\n", what, strerror(err));
    exit(2);
}

static void *player(void *arg) {
    int me = (int)(long)arg;
    for (int i = 0; i < ROUNDS; i++) {
        pthread_mutex_lock(&m);
        while (turn != me)
            pthread_cond_wait(&cv, &m);   /* ปล่อย m ข้างใน glibc — เราไม่เห็น */
        played[me]++;
        turn = 1 - me;
        pthread_cond_signal(&cv);
        pthread_mutex_unlock(&m);         /* ตารางอาจจดว่าอีกคนถือ m อยู่ */
    }
    return NULL;
}

int main(void) {
    pthread_t t[2];
    int rc;

    for (long i = 0; i < 2; i++) {
        rc = pthread_create(&t[i], NULL, player, (void *)i);
        if (rc != 0) die("pthread_create", rc);
    }
    for (int i = 0; i < 2; i++) {
        rc = pthread_join(t[i], NULL);
        if (rc != 0) die("pthread_join", rc);
    }

    if (played[0] != ROUNDS || played[1] != ROUNDS) {
        printf("FAIL: played = %d/%d ต้องได้ %d ทั้งคู่\n", played[0], played[1], ROUNDS);
        return 1;
    }
    printf("PASS: ผลัดกันครบ %d รอบต่อ thread\n", ROUNDS);
    return 0;
}
