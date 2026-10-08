#!/usr/bin/env bash
# run_demo.sh — ลำดับการ demo ตอนนำเสนอ (ดู docs/overview.html หัวข้อ 5)
#
# รันจาก terminal จริง -> หยุดรอกด Enter ระหว่างแต่ละขั้น เหมาะกับตอน present
# รันจากสคริปต์/ไปป์   -> วิ่งรวดเดียวจนจบ เหมาะกับการเช็คว่าทุกอย่างยังทำงาน
set -u
cd "$(dirname "$0")/.." || exit 1

LIB="$PWD/build/libdetect.so"
DOT="$PWD/cycle.dot"
PNG="$PWD/cycle.png"

banner() {
    echo
    echo "=================================================================="
    echo "  $1"
    echo "=================================================================="
    echo
}

pause() {
    if [ -t 0 ]; then
        read -r -p "   [กด Enter เพื่อไปต่อ] " _
    fi
}

# ---- build ถ้ายังไม่มีของ ----
if [ ! -f "$LIB" ] || [ ! -x ./build/bank ] || [ ! -x ./build/no_deadlock ]; then
    banner "build ก่อน"
    make || { echo "make ไม่ผ่าน"; exit 1; }
fi

# ---------------------------------------------------------------- 1
banner "1) รันโปรแกรมโอนเงินธรรมดา"
echo '   $ ./build/bank'
echo
timeout 3 ./build/bank
echo
echo "   ^ ค้างนิ่งจนถูกตัดที่ 3 วินาที"
echo "     ไม่ crash ไม่มี error message ไม่มี stack trace  CPU 0%"
echo "     นี่คือทั้งหมดที่ deadlock บอกเรา"
pause

# ---------------------------------------------------------------- 2
banner "2) คำสั่งเดิมเป๊ะ แค่เติม LD_PRELOAD ข้างหน้า"
echo '   $ LD_PRELOAD=./build/libdetect.so ./build/bank'
echo
LD_PRELOAD="$LIB" timeout 3 ./build/bank
echo
echo "   ^ บอกครบ: thread ไหนถืออะไร รออะไร และบรรทัดไหนในซอร์ส"
echo
echo "   เทียบกับซอร์สจริง:"
grep -n 'pthread_mutex_lock' tests/bank.c | sed 's/^/     /'
pause

# ---------------------------------------------------------------- 3
banner "3) export wait-for graph เป็นภาพ"
echo '   $ DD_DOT_OUT=cycle.dot LD_PRELOAD=./build/libdetect.so ./build/bank'
echo
rm -f "$DOT" "$PNG"
DD_DOT_OUT="$DOT" LD_PRELOAD="$LIB" timeout 3 ./build/bank >/dev/null 2>&1
if [ -f "$DOT" ]; then
    sed 's/^/     /' "$DOT"
    echo
    if command -v dot >/dev/null 2>&1; then
        dot -Tpng "$DOT" -o "$PNG" && echo "   -> เขียน cycle.png แล้ว เปิดดูได้เลย"
    else
        echo "   (ไม่มีคำสั่ง dot — ติดตั้งด้วย: sudo apt install graphviz)"
    fi
else
    echo "   ไม่ได้ไฟล์ .dot"
fi
echo
echo "   วงรี = thread  สี่เหลี่ยม = mutex"
echo "   T -> M คือกำลังขอ (แดง)   M -> T คือถูกถืออยู่"
echo "   ลูกศรวนครบรอบ = deadlock  ตรงกับ resource allocation graph ในสไลด์"
pause

# ---------------------------------------------------------------- 4
banner "4) เวอร์ชันที่แก้แล้ว — ล็อกตามลำดับสากล"
echo '   $ LD_PRELOAD=./build/libdetect.so ./build/no_deadlock'
echo
LD_PRELOAD="$LIB" timeout 15 ./build/no_deadlock
echo
echo "   ^ โค้ดเดียวกับ bank.c ทุกอย่าง หน่วงเวลาเท่ากันเป๊ะ"
echo "     ต่างแค่ล็อกตามลำดับเดียวกันทั้งสอง thread"
echo "     -> จบเอง ไม่ค้าง และเครื่องมือเงียบสนิท (ไม่เตือนมั่ว)"
pause

# ---------------------------------------------------------------- 5
banner "5) ใช้กับโปรแกรมที่เราไม่ได้เขียนเอง"
fail=0
for prog in "ls /usr/bin" "git --version" "make --version" "gcc --version"; do
    if LD_PRELOAD="$LIB" timeout 15 $prog >/dev/null 2>&1; then
        echo "   OK    $prog"
    else
        echo "   FAIL  $prog"
        fail=1
    fi
done
echo
echo "   ^ ไม่ต้องแก้โค้ด ไม่ต้อง compile ใหม่ ไม่ต้องมีซอร์สด้วยซ้ำ"
echo "     ขอแค่ลิงก์ libpthread แบบ dynamic"

banner "จบ demo"
if [ "$fail" -eq 0 ]; then
    echo "   ผ่านทุกขั้น"
else
    echo "   มีบางขั้นไม่ผ่าน ดูข้างบน"
fi
echo
exit "$fail"
