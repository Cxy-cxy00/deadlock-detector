/* sample_webapp.c — ตัวอย่างไว้ลากเข้าหน้าเว็บ playground
 *
 * จงใจเขียนให้ "ดูเหมือนโค้ดจริง" มากกว่า bank.c เพื่อให้เห็นว่า deadlock
 * ในชีวิตจริงมักไม่ได้หน้าตาชัดเจนแบบในตำรา — มันซ่อนอยู่ระหว่างสองฟังก์ชัน
 * ที่คนละคนเขียน คนละเวลา และต่างคนต่างดูแล้วไม่เห็นว่าผิดตรงไหน
 *
 * เรื่องย่อ: เซิร์ฟเวอร์เล็ก ๆ ที่มีของใช้ร่วมกันสองอย่าง
 *   - pool  : คิวคอนเนกชัน      มี mutex ของตัวเอง
 *   - stats : ตัวนับสถิติ        มี mutex ของตัวเอง
 *
 * สอง thread ทำงานคนละหน้าที่ และบังเอิญล็อกสวนลำดับกัน
 *
 *   worker  : handle_request()   ล็อก pool  ก่อน แล้วค่อย stats
 *   metrics : report_metrics()   ล็อก stats ก่อน แล้วค่อย pool
 *
 * แต่ละฟังก์ชันอ่านแยกกันดูสมเหตุสมผลทั้งคู่ ความผิดเกิดตอนรันพร้อมกันเท่านั้น
 * และจะโผล่เฉพาะตอนจังหวะพอดี ซึ่งในของจริงแปลว่า "เซิร์ฟเวอร์ค้างสัปดาห์ละครั้ง
 * ทำซ้ำไม่ได้ หาไม่เจอ"  ที่นี่ใส่ usleep ไว้บังคับให้เกิดทุกครั้ง
 *
 * วิธีใช้
 *   1. เปิด http://127.0.0.1:8000  (สั่ง make playground ก่อน)
 *   2. ลากไฟล์นี้ไปวางบนช่องโค้ด
 *   3. กด "รันแบบปกติ"      -> ค้างเงียบ ๆ ไม่มีอะไรบอก
 *   4. กด "รันผ่านเครื่องมือ" -> บอกว่าใครค้างกับใคร ที่บรรทัดไหน
 *
 * วิธีแก้อยู่ท้ายไฟล์
 */
#define _DEFAULT_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#define SLOW_WORK_US 120000   /* 120 ms — แทนงานที่ใช้เวลาจริง เช่น query ฐานข้อมูล */

/* ---- ของใช้ร่วมกันสองอย่าง แต่ละอันมี mutex ของตัวเอง ---- */

struct conn_pool {
    pthread_mutex_t lock;
    int             active;      /* จำนวนคอนเนกชันที่ใช้อยู่ */
    int             total;
};

struct server_stats {
    pthread_mutex_t lock;
    long            requests;
    long            errors;
};

static struct conn_pool pool = {
    .lock = PTHREAD_MUTEX_INITIALIZER, .active = 0, .total = 16
};

static struct server_stats stats = {
    .lock = PTHREAD_MUTEX_INITIALIZER, .requests = 0, .errors = 0
};

/* ================================================================
 * ฝั่ง worker — รับ request เข้ามาแล้วอัปเดตสถิติ
 * ลำดับที่ใช้:  pool -> stats
 * ================================================================ */
static void handle_request(void)
{
    pthread_mutex_lock(&pool.lock);          /* [1] จอง connection */
    pool.active++;
    printf("  [worker]  จอง connection แล้ว (active=%d)\n", pool.active);
    fflush(stdout);

    usleep(SLOW_WORK_US);                    /* ทำงานจริง เช่น query ฐานข้อมูล */

    printf("  [worker]  ขอ lock ของ stats เพื่อบวกตัวนับ ...\n");
    fflush(stdout);
    pthread_mutex_lock(&stats.lock);         /* [2] ค้างตรงนี้ */
    stats.requests++;
    pthread_mutex_unlock(&stats.lock);

    pool.active--;
    pthread_mutex_unlock(&pool.lock);
}

/* ================================================================
 * ฝั่ง metrics — ตัวเก็บสถิติที่รันเป็นคาบ (นึกถึง /metrics ของ Prometheus)
 * ลำดับที่ใช้:  stats -> pool     <-- สวนกับฝั่งบน นี่คือต้นเหตุทั้งหมด
 * ================================================================ */
static void report_metrics(void)
{
    pthread_mutex_lock(&stats.lock);         /* [3] อ่านตัวนับก่อน */
    long seen = stats.requests;
    printf("  [metrics] อ่านตัวนับแล้ว (requests=%ld)\n", seen);
    fflush(stdout);

    usleep(SLOW_WORK_US);                    /* จัดรูปแบบข้อความรายงาน */

    printf("  [metrics] ขอ lock ของ pool เพื่อดูจำนวน connection ...\n");
    fflush(stdout);
    pthread_mutex_lock(&pool.lock);          /* [4] ค้างตรงนี้ */
    printf("  [metrics] requests=%ld active=%d/%d\n", seen, pool.active, pool.total);
    fflush(stdout);
    pthread_mutex_unlock(&pool.lock);

    pthread_mutex_unlock(&stats.lock);
}

/* ---------------------------------------------------------------- */

static void *worker_thread(void *arg)
{
    (void)arg;
    handle_request();
    printf("  [worker]  เสร็จงาน\n");
    fflush(stdout);
    return NULL;
}

static void *metrics_thread(void *arg)
{
    (void)arg;
    report_metrics();
    printf("  [metrics] เสร็จงาน\n");
    fflush(stdout);
    return NULL;
}

int main(void)
{
    pthread_t worker, metrics;

    printf("sample_webapp: จำลองเซิร์ฟเวอร์ที่มี worker กับตัวเก็บสถิติ\n");
    printf("               ถ้าเงียบไปเกิน 1 วินาที = ค้างแล้ว\n\n");
    fflush(stdout);

    pthread_create(&worker, NULL, worker_thread, NULL);
    pthread_create(&metrics, NULL, metrics_thread, NULL);

    pthread_join(worker, NULL);
    pthread_join(metrics, NULL);

    /* สองบรรทัดนี้จะถูกพิมพ์ก็ต่อเมื่อ deadlock ไม่เกิด */
    printf("\nจบงานทั้งหมด requests=%ld errors=%ld\n", stats.requests, stats.errors);
    return 0;
}

/* ================================================================
 * แก้ยังไง
 * ================================================================
 * ตกลงกันทั้งโปรเจ็คว่าจะล็อกตาม "ลำดับสากล" ลำดับเดียว แล้วทุกที่ทำตามนั้น
 * เช่นกำหนดว่า pool ต้องมาก่อน stats เสมอ แล้วแก้ report_metrics ให้เป็น
 *
 *     pthread_mutex_lock(&pool.lock);
 *     pthread_mutex_lock(&stats.lock);
 *     ... อ่านค่าที่ต้องใช้ ...
 *     pthread_mutex_unlock(&stats.lock);
 *     pthread_mutex_unlock(&pool.lock);
 *
 * หรือทางที่ดีกว่าในของจริง: อย่าถือสอง lock พร้อมกันตั้งแต่แรก
 * ให้ copy ค่าที่ต้องใช้ออกมาก่อนแล้วปล่อย lock แล้วค่อยไปล็อกอีกตัว
 *
 * นี่คือ deadlock prevention แบบทำลายเงื่อนไข circular wait ของ Coffman
 * — ดู tests/no_deadlock.c ประกอบ
 * ================================================================ */
