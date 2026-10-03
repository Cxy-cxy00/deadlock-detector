/* graph.h — wait-for graph และการหา cycle  (ผู้รับผิดชอบ: คนที่ 3)
 *
 * node = thread,  edge T1 -> T2 = "T1 รอ mutex ที่ T2 ถืออยู่"
 * มี cycle = เกิด deadlock แล้ว (ตรงตามบท Deadlocks)
 */
#ifndef DD_GRAPH_H
#define DD_GRAPH_H

#include "common.h"

#define DD_MAX_CYCLE_LEN 64

/* cycle หนึ่งวง: tids[0] -> tids[1] -> ... -> tids[len-1] -> tids[0] */
typedef struct {
    dd_tid_t   tids[DD_MAX_CYCLE_LEN];
    dd_mutex_t mutexes[DD_MAX_CYCLE_LEN];  /* mutex ที่ tids[i] รออยู่ */
    size_t     len;
} dd_cycle_t;

/* สร้างกราฟจากตารางใน state.c (snapshot ครั้งเดียว ใต้ dd_state_lock) */
void dd_graph_build(void);

/* หา cycle ด้วย DFS + สี (white/gray/black)
 * คืน 1 ถ้าเจอและเขียนผลลง out, คืน 0 ถ้าไม่เจอ
 */
int dd_graph_find_cycle(dd_cycle_t *out);

#endif /* DD_GRAPH_H */
