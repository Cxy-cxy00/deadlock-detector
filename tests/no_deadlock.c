/* no_deadlock.c — เวอร์ชันที่แก้แล้ว ใช้ยืนยันว่าเครื่องมือไม่รายงานผิด (false positive)
 * (ผู้รับผิดชอบ: คนที่ 5)
 *
 * TODO
 *  - โค้ดเดียวกับ bank.c แต่ล็อกตาม global order เสมอ
 *    (เรียงตามที่อยู่ของ mutex หรือตามเลขบัญชี)
 *  - รันผ่าน LD_PRELOAD แล้วต้องไม่มีรายงานออกมา
 */
#include <stdio.h>

int main(void) {
    printf("TODO: ordered-lock version, must NOT be reported\n");
    return 0;
}
