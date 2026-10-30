/* report.h — พิมพ์รายงานและ export Graphviz  (ผู้รับผิดชอบ: คนที่ 5)
 */
#ifndef DD_REPORT_H
#define DD_REPORT_H

#include "graph.h"
#include "state.h"

/* พิมพ์รายงาน [DEADLOCK DETECTED] ออก stderr ตามรูปแบบในเอกสาร docs/overview.html */
void dd_report_cycle(const dd_cycle_t *c);

/* พิมพ์คำเตือน [LOCK ORDER INVERSION] — สอง mutex ถูกล็อกสลับลำดับกัน (ยังไม่ค้าง) */
void dd_report_inversion(const dd_inversion_t *v);

/* เขียน wait-for graph ของทุกวงใน cs[0..n-1] เป็นไฟล์ .dot ไฟล์เดียว
 * (path NULL = ใช้ค่าจาก env DD_DOT_OUT)
 */
void dd_report_dot(const dd_cycle_t *cs, size_t n, const char *path);

/* แปลง pc -> file:line  (backtrace + addr2line หรือ libbacktrace) */
void dd_report_resolve_site(dd_site_t *site);

#endif /* DD_REPORT_H */
