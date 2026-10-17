#!/usr/bin/env bash
# run_demo.sh — ลำดับการ demo ตอนนำเสนอ (ดู docs/overview.html หัวข้อ 5)
#
#   bash tests/run_demo.sh              หยุดรอกด Enter ระหว่างแต่ละขั้น (ตอน present)
#   bash tests/run_demo.sh --no-pause   วิ่งรวดเดียวจนจบ (เช็คอัตโนมัติ)
#   bash tests/run_demo.sh --smoke      เพิ่มขั้น 5: โหลดกับโปรแกรมอื่นได้โดยไม่พัง
#
# ทุกขั้นมีเงื่อนไขผ่าน/ไม่ผ่านจริง ไม่ได้แค่พิมพ์ผลให้ดู
# ต้องผ่านครบทุกขั้นถึงจะพิมพ์ "ผ่านทุกขั้น" และ exit 0  ไม่ผ่านแม้ขั้นเดียว = exit 1
set -u
cd "$(dirname "$0")/.." || exit 1

NO_PAUSE=0
SMOKE=0
for arg in "$@"; do
    case "$arg" in
        --no-pause) NO_PAUSE=1 ;;
        --smoke)    SMOKE=1 ;;
        -h|--help)  sed -n '2,9p' "$0"; exit 0 ;;
        *)          echo "ไม่รู้จัก option: $arg (ดู --help)"; exit 2 ;;
    esac
done

# ใช้ path แบบ relative โดยตั้งใจ — ld.so แยก LD_PRELOAD ด้วยช่องว่างและ ':'
# ถ้าโฟลเดอร์โปรเจกต์มีช่องว่าง (เช่น ".../Bachelor degree/...") path แบบเต็มจะโหลดไม่ขึ้น
# แล้ว demo จะ "ไม่เจอ deadlock" แบบเงียบ ๆ  (สคริปต์ cd มาที่รากโปรเจกต์แล้ว relative จึงถูกเสมอ)
LIB="./build/libdetect.so"
DOT="$PWD/cycle.dot"
PNG="$PWD/cycle.png"
MARK='[DEADLOCK DETECTED]'
ERR="$(mktemp)"
trap 'rm -f "$ERR"' EXIT

fail=0

banner() {
    echo
    echo "=================================================================="
    echo "  $1"
    echo "=================================================================="
    echo
}

pause() {
    if [ "$NO_PAUSE" -eq 0 ] && [ -t 0 ]; then
        read -r -p "   [กด Enter เพื่อไปต่อ] " _
    fi
}

ok()  { echo "   [ผ่าน]     $1"; }
bad() { echo "   [ไม่ผ่าน]  $1"; fail=1; }

# รันคำสั่ง: stdout/stderr ขึ้นจอตามปกติ และเก็บ stderr ไว้ใน $ERR ด้วย  exit code อยู่ใน RC
RC=0
run_capture() {
    { "$@" 2>&1 1>&3 | tee "$ERR" >&2; RC=${PIPESTATUS[0]}; } 3>&1
}

has_marker() { grep -qF "$MARK" "$ERR"; }

# ---- build ทุกครั้ง: กันกรณีแก้ซอร์สแล้วลืม build ทำให้ demo ใช้ของเก่า ----
banner "build"
make || { echo "   make ไม่ผ่าน"; exit 1; }

# ---------------------------------------------------------------- 1
banner "1) รันโปรแกรมโอนเงินธรรมดา"
echo '   $ ./build/bank'
echo
run_capture timeout 3 ./build/bank
echo
echo "   ^ ค้างนิ่งจนถูกตัดที่ 3 วินาที"
echo "     ไม่ crash ไม่มี error message ไม่มี stack trace  CPU 0%"
echo "     นี่คือทั้งหมดที่ deadlock บอกเรา"
if [ "$RC" -eq 124 ]; then ok "ค้างจนหมดเวลา (exit 124)"; else bad "ควรค้างจนหมดเวลา (exit 124) แต่ได้ exit $RC"; fi
pause

# ---------------------------------------------------------------- 2
banner "2) คำสั่งเดิมเป๊ะ แค่เติม LD_PRELOAD ข้างหน้า"
echo "   \$ LD_PRELOAD=$LIB ./build/bank"
echo
run_capture timeout 3 env LD_PRELOAD="$LIB" ./build/bank
echo
echo "   ^ บอกครบ: thread ไหนถืออะไร รออะไร และบรรทัดไหนในซอร์ส"
echo
echo "   เทียบกับซอร์สจริง:"
grep -n 'pthread_mutex_lock' tests/bank.c | sed 's/^/     /'
if [ "$RC" -eq 124 ]; then ok "ยังค้างอยู่ (exit 124) — เครื่องมือตรวจ ไม่ได้แก้"; else bad "ควรได้ exit 124 แต่ได้ exit $RC"; fi
if has_marker; then ok "stderr มี $MARK"; else bad "stderr ไม่มี $MARK — libdetect.so ไม่ได้ทำงาน"; fi
pause

# ---------------------------------------------------------------- 3
banner "3) export wait-for graph เป็นภาพ"
echo "   \$ DD_DOT_OUT=cycle.dot LD_PRELOAD=$LIB ./build/bank"
echo
rm -f "$DOT" "$PNG"
DD_DOT_OUT="$DOT" timeout 3 env LD_PRELOAD="$LIB" ./build/bank >/dev/null 2>&1
if [ -s "$DOT" ]; then
    sed 's/^/     /' "$DOT"
    echo
    ok "ได้ไฟล์ cycle.dot และไม่ว่าง"
    if command -v dot >/dev/null 2>&1; then
        dot -Tpng "$DOT" -o "$PNG" && echo "   -> เขียน cycle.png แล้ว เปิดดูได้เลย"
    else
        echo "   (ไม่มีคำสั่ง dot — ติดตั้งด้วย: sudo apt install graphviz)"
    fi
else
    bad "ไม่ได้ไฟล์ cycle.dot หรือไฟล์ว่าง"
fi
echo
echo "   วงรี = thread  สี่เหลี่ยม = mutex"
echo "   T -> M คือกำลังขอ (แดง)   M -> T คือถูกถืออยู่"
echo "   ลูกศรวนครบรอบ = circular wait  ตรงกับ resource allocation graph ในสไลด์"
pause

# ---------------------------------------------------------------- 4
banner "4) เวอร์ชันที่แก้แล้ว — ล็อกตามลำดับสากล"
echo "   \$ LD_PRELOAD=$LIB ./build/no_deadlock"
echo
run_capture timeout 15 env LD_PRELOAD="$LIB" ./build/no_deadlock
echo
echo "   ^ บัญชีเดียวกัน โอนสวนทางกันเหมือนกัน"
echo "     ต่างแค่ทุก thread ล็อกตามลำดับสากลเดียวกัน"
if [ "$RC" -eq 0 ]; then ok "จบเองไม่ค้าง (exit 0)"; else bad "ควรจบด้วย exit 0 แต่ได้ exit $RC"; fi
if has_marker; then bad "มี $MARK — เตือนผิด (false positive)"; else ok "ไม่มี $MARK (ไม่เตือนผิด)"; fi
pause

# ---------------------------------------------------------------- 5 (optional)
if [ "$SMOKE" -eq 1 ]; then
    banner "5) smoke test: โหลดกับโปรแกรมอื่นได้โดยไม่พัง"
    for prog in "ls /usr/bin" "git --version" "make --version" "gcc --version"; do
        # shellcheck disable=SC2086  # ตั้งใจให้แยกคำ
        if timeout 15 env LD_PRELOAD="$LIB" $prog >/dev/null 2>&1; then
            ok "$prog"
        else
            bad "$prog"
        fi
    done
    echo
    echo "   ^ ยืนยันแค่ว่า library โหลดเข้าโปรแกรมอื่นแล้วโปรแกรมยังทำงานจบปกติ"
    echo "     ไม่ได้พิสูจน์ว่าตรวจ deadlock ในโปรแกรมเหล่านี้ได้ (มันไม่มี deadlock ให้ตรวจ)"
fi

banner "จบ demo"
if [ "$fail" -eq 0 ]; then
    echo "   ผ่านทุกขั้น"
    echo
    exit 0
fi
echo "   มีบางขั้นไม่ผ่าน ดู [ไม่ผ่าน] ข้างบน"
echo
exit 1
