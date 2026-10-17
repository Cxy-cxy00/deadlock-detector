/* graph.h — wait-for graph และการหา cycle  (ผู้รับผิดชอบ: คนที่ 3)
 *
 * node = thread,  edge T1 -> T2 = "T1 รอ mutex ที่ T2 ถืออยู่"
 * มี cycle = เกิด deadlock แล้ว (ตรงตามบท Deadlocks)
 */
#ifndef DD_GRAPH_H
#define DD_GRAPH_H

#include "common.h"

#define DD_MAX_CYCLE_LEN 64

/* cycle หนึ่งวง: tids[0] -> tids[1] -> ... -> tids[len-1] -> tids[0]
 *
 * ถ้าวงจริงยาวเกิน DD_MAX_CYCLE_LEN จะเก็บได้แค่ช่วงแรก: partial = 1
 * ตอนนั้น tids[len-1] ไม่ได้รอ tids[0] จริง ๆ (รอ thread ที่ถูกตัดทิ้งไป)
 * ผู้ใช้ข้อมูลห้ามวาด/พิมพ์ลูกศรปิดวงกลับไป tids[0]
 */
typedef struct {
    dd_tid_t   tids[DD_MAX_CYCLE_LEN];
    dd_mutex_t mutexes[DD_MAX_CYCLE_LEN];  /* mutex ที่ tids[i] รออยู่ */
    size_t     len;                        /* จำนวนที่เก็บไว้จริง */
    size_t     full_len;                   /* ความยาววงจริง (== len ถ้าไม่ถูกตัด) */
    int        partial;                    /* 1 = ถูกตัด เป็นแค่เส้นทางส่วนหนึ่งของวง */
} dd_cycle_t;

/* สร้างกราฟจากตารางใน state.c (snapshot ครั้งเดียว ใต้ dd_state_lock) */
void dd_graph_build(void);

/* หา cycle ด้วย DFS + สี (white/gray/black)
 * คืน 1 ถ้าเจอและเขียนผลลง out, คืน 0 ถ้าไม่เจอ
 */
int dd_graph_find_cycle(dd_cycle_t *out);

#endif /* DD_GRAPH_H */
