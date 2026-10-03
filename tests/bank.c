/* bank.c — โปรแกรมทดสอบตัวหลัก: โอนเงินสองทิศทางพร้อมกันจนเกิด deadlock
 * (ผู้รับผิดชอบ: คนที่ 5)
 *
 * TODO
 *  - สองบัญชี แต่ละบัญชีมี mutex ของตัวเอง
 *  - thread 1: lock(A) -> usleep -> lock(B)      <- usleep ทำให้ deadlock เกิดแน่
 *  - thread 2: lock(B) -> usleep -> lock(A)
 *  - compile ด้วย -g เพื่อให้ตัว detector map บรรทัดได้
 *
 * บรรทัดที่เรียก lock ในไฟล์นี้คือบรรทัดที่รายงานต้องชี้ให้ถูก
 */
#include <stdio.h>

int main(void) {
    printf("TODO: bank deadlock demo\n");
    return 0;
}
