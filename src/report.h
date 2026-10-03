/* report.h — พิมพ์รายงานและ export Graphviz  (ผู้รับผิดชอบ: คนที่ 5)
 */
#ifndef DD_REPORT_H
#define DD_REPORT_H

#include "graph.h"

/* พิมพ์รายงาน [DEADLOCK DETECTED] ออก stderr ตามรูปแบบในเอกสาร docs/overview.html */
void dd_report_cycle(const dd_cycle_t *c);

/* เขียน wait-for graph เป็นไฟล์ .dot  (path NULL = ใช้ค่าจาก env DD_DOT_OUT) */
void dd_report_dot(const dd_cycle_t *c, const char *path);

/* แปลง pc -> file:line  (backtrace + addr2line หรือ libbacktrace) */
void dd_report_resolve_site(dd_site_t *site);

#endif /* DD_REPORT_H */
