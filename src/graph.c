/* graph.c — ยังไม่ implement  (ผู้รับผิดชอบ: คนที่ 3)
 *
 * TODO
 *  - dd_graph_build: วน dd_state_waits() แล้วต่อ edge waiter -> dd_state_holder_of(mutex)
 *  - dd_graph_find_cycle: DFS ย้อนรอย เจอ node สีเทาซ้ำ = cycle, เก็บเส้นทางลง out
 *  - เทียบกับ resource allocation graph ในสไลด์: เราย่อ mutex ออกไปเหลือแต่ thread
 */
#include "graph.h"
#include "state.h"

void dd_graph_build(void) { /* TODO */ }

int dd_graph_find_cycle(dd_cycle_t *out) { (void)out; return 0; /* TODO */ }
