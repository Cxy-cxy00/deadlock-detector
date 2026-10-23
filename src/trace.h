/* trace.h — บันทึกเหตุการณ์ทีละขั้นลงไฟล์ (env DD_TRACE) ให้หน้า playground เล่นเป็น timeline
 *
 * ไม่ได้ตั้ง DD_TRACE = ทุกฟังก์ชันคืนทันที ไม่เสีย overhead ใน lock path
 * รูปแบบไฟล์ (หนึ่งบรรทัดต่อหนึ่งเหตุการณ์ คั่นด้วยช่องว่าง):
 *
 *   # dd-trace 1
 *   base <hex>                                  load base ของโปรแกรมเป้าหมาย (แปลง pc -> บรรทัด)
 *   <seq> <us> wait   <tid> <mutex> <pc>        เริ่มรอ (wait_begin)
 *   <seq> <us> unwait <tid> <mutex>             เลิกรอ (wait_end)
 *   <seq> <us> acq    <tid> <mutex> <pc>        ได้ lock (lock หรือ trylock)
 *   <seq> <us> rel    <tid> <mutex>             ปล่อย lock
 *   <seq> <us> relx   <tid> <mutex> <holder>    unlock โดยคนที่ไม่ใช่เจ้าของในตาราง — คงเจ้าของเดิม
 *   <seq> <us> trunc                            เหตุการณ์ lock เกิน DD_TRACE_MAX_EVENTS หยุดจดส่วน lock
 *   <seq> <us> check  <round> <nedges>          detector ถ่าย snapshot ตาราง สร้างกราฟได้ nedges เส้น
 *   <seq> <us> cycle  <round> <fresh> <len> <full_len> <partial> <tid>:<mutex> ...
 *
 * seq คือลำดับรวมทั้งไฟล์ (บรรทัดจากหลาย thread อาจสลับกันในไฟล์ ให้เรียงด้วย seq)
 * us คือไมโครวินาทีนับจากตอนโหลด library
 */
#ifndef DD_TRACE_H
#define DD_TRACE_H

#include "common.h"
#include "graph.h"

/* เปิดไฟล์ตาม DD_TRACE — เรียกครั้งเดียวจาก constructor ตอนที่ยังมี thread เดียว */
void dd_trace_init(void);

/* fd ของไฟล์ trace (-1 = ปิด) ให้ state.c เช็คเองก่อนเรียก
 * ถ้าไปเช็คข้างในฟังก์ชัน ทุก lock+unlock ต้องเสีย function call ข้ามไฟล์ 4 ครั้งทั้งที่ปิดอยู่
 * (วัดแล้วช้าลงราว 10%)  hidden = ไม่ต้องอ้อมผ่าน GOT ของ shared object
 */
extern int dd_trace_fd __attribute__((visibility("hidden")));
#define DD_TRACING() __builtin_expect(dd_trace_fd >= 0, 0)

/* เหตุการณ์ของ thread เป้าหมาย — state.c เรียกขณะถือ dd_state_lock
 * ลำดับในไฟล์จึงตรงกับลำดับที่ตารางเปลี่ยนจริง
 */
void dd_trace_lock_event(const char *kind, dd_tid_t t, dd_mutex_t m, void *pc);
void dd_trace_relx(dd_tid_t t, dd_mutex_t m, dd_tid_t holder);

/* เหตุการณ์ของ detector thread เท่านั้น
 * check เรียกจาก graph.c ใต้ dd_state_lock (ตอน snapshot) แล้ว cycle ของรอบนั้นตามมา
 */
void dd_trace_check(size_t nedges);
void dd_trace_cycle(const dd_cycle_t *c, int fresh);

#endif /* DD_TRACE_H */
