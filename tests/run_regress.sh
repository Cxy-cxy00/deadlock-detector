#!/usr/bin/env bash
# run_regress.sh — regression test ของกรณีขอบที่เคยแก้ (ไม่ใช่ส่วนของ demo ตอน present)
#
#   bash tests/run_regress.sh      หรือ   make regress
#
# แต่ละข้อมีเงื่อนไขผ่าน/ไม่ผ่านจริง  ผ่านครบ = exit 0  ไม่ผ่านแม้ข้อเดียว = exit 1
#   robust     ข้อ 2.3  robust mutex คืน EOWNERDEAD ต้องนับว่าได้ lock
#   errcheck   ข้อ 2.4  unlock โดยคนที่ไม่ใช่เจ้าของ (EPERM) ต้องไม่ลบเจ้าของเดิม
#   longcycle  ข้อ 2.7  cycle ยาว 70 รายงานเป็น partial path ไม่ปิดวงปลอม
#   condvar    ข้อ 5.1  โปรแกรม condvar ที่ไม่มี deadlock ต้องไม่มีรายงาน และเตือนแค่ครั้งเดียว
#   trace      DD_TRACE เขียน timeline ครบ และไม่ถูก process ลูก (addr2line) เขียนทับ
#   lockorder  ข้อ 7.1  ล็อกสลับลำดับแต่ยังไม่ค้าง ต้องเตือน inversion ครั้งเดียว
#              no_deadlock ห้ามเตือน · bank เตือนแค่ deadlock ไม่เตือน inversion ซ้ำ
set -u
cd "$(dirname "$0")/.." || exit 1

# path แบบ relative โดยตั้งใจ — เหตุผลเดียวกับ run_demo.sh (ld.so แยก LD_PRELOAD ตรงช่องว่าง)
LIB="./build/libdetect.so"
MARK='[DEADLOCK DETECTED]'
ERR="$(mktemp)"
OUT="$(mktemp)"
trap 'rm -f "$ERR" "$OUT"' EXIT

fail=0
ok()  { echo "   [ผ่าน]     $1"; }
bad() { echo "   [ไม่ผ่าน]  $1"; fail=1; }

# รันผ่าน LD_PRELOAD เก็บ stdout/stderr แยกไฟล์  exit code อยู่ใน RC
RC=0
run_preload() {
    local secs="$1"; shift
    timeout "$secs" env LD_PRELOAD="$LIB" "$@" >"$OUT" 2>"$ERR"
    RC=$?
}

count() { grep -cF -- "$1" "$ERR"; }

make >/dev/null || { echo "make ไม่ผ่าน"; exit 1; }

echo "== robust (ข้อ 2.3)"
run_preload 5 ./build/robust
grep -q "rc=130 (EOWNERDEAD)" "$OUT" && ok "main ได้ EOWNERDEAD" || bad "main ไม่ได้ EOWNERDEAD: $(head -3 "$OUT")"
[ "$RC" -eq 124 ] && ok "ค้าง (exit 124)" || bad "ควร exit 124 แต่ได้ $RC"
[ "$(count "$MARK 2 threads in cycle")" -eq 1 ] && ok "รายงาน deadlock 2 thread หนึ่งครั้ง" \
    || bad "ควรมีรายงาน 2 threads in cycle หนึ่งครั้ง"

echo "== errcheck (ข้อ 2.4)"
run_preload 5 ./build/errcheck
grep -q "rc=1 (EPERM)" "$OUT" && ok "t2 unlock ได้ EPERM" || bad "t2 ไม่ได้ EPERM: $(head -3 "$OUT")"
[ "$RC" -eq 124 ] && ok "ค้าง (exit 124)" || bad "ควร exit 124 แต่ได้ $RC"
[ "$(count "$MARK 2 threads in cycle")" -eq 1 ] && ok "รายงาน deadlock 2 thread หนึ่งครั้ง" \
    || bad "ควรมีรายงาน 2 threads in cycle หนึ่งครั้ง"
[ "$(count "WARNING")" -eq 1 ] && ok "WARNING เรื่องเจ้าของไม่ตรง 1 ครั้ง" \
    || bad "ควรมี WARNING 1 ครั้ง ได้ $(count "WARNING")"

echo "== longcycle (ข้อ 2.7)"
run_preload 20 ./build/longcycle
[ "$RC" -eq 124 ] && ok "ค้าง (exit 124)" || bad "ควร exit 124 แต่ได้ $RC"
[ "$(count "$MARK 70 threads in cycle (partial path, truncated: แสดง 64)")" -eq 1 ] \
    && ok "หัวรายงานบอกว่ายาว 70 แสดง 64" || bad "หัวรายงาน partial ไม่ถูก"
grep -qF -- "-> ... (partial path, truncated)" "$ERR" && ok "บรรทัด Cycle จบด้วย ... ไม่ปิดวง" \
    || bad "บรรทัด Cycle ไม่จบด้วย '... (partial path, truncated)'"

echo "== condvar (ข้อ 5.1)"
run_preload 20 ./build/condvar
[ "$RC" -eq 0 ] && ok "จบเอง (exit 0)" || bad "ควร exit 0 แต่ได้ $RC"
[ "$(count "$MARK")" -eq 0 ] && ok "ไม่มี [DEADLOCK DETECTED] (ไม่เตือนผิด)" || bad "มีรายงาน deadlock ทั้งที่ไม่มีจริง"
[ "$(count "WARNING")" -le 1 ] && ok "WARNING ไม่เกิน 1 ครั้ง (ได้ $(count "WARNING"))" \
    || bad "WARNING ท่วม: $(count "WARNING") ครั้ง"

echo "== trace (timeline ของหน้า playground)"
TRACE="$(mktemp)"
trap 'rm -f "$ERR" "$OUT" "$TRACE"' EXIT
DD_TRACE="$TRACE" run_preload 3 ./build/bank
head -1 "$TRACE" | grep -qxF "# dd-trace 1" && ok "มีหัวไฟล์ trace" || bad "ไฟล์ trace ว่างหรือไม่มีหัวไฟล์"
# รายงานเรียก addr2line ผ่าน popen ซึ่งได้ LD_PRELOAD ไปด้วย ถ้า DD_TRACE ตกทอดไปถึงลูก
# constructor ของลูกจะเปิดไฟล์ด้วย O_TRUNC ทับ timeline ทิ้ง เหลือแค่หัวไฟล์ของลูก
# (ถอย unsetenv ใน trace.c ออกแล้วข้อนี้ได้ 0 — ทดสอบแล้ว)
[ "$(grep -c ' wait ' "$TRACE")" -eq 4 ] && ok "wait ครบ 4 ครั้ง — ไม่ถูก process ลูกเขียนทับ" \
    || bad "ควรมี wait 4 ครั้ง ได้ $(grep -c ' wait ' "$TRACE")"
[ "$(grep -c ' cycle [0-9]* 1 2 2 0 ' "$TRACE")" -eq 1 ] && ok "cycle ใหม่ยาว 2 หนึ่งครั้ง" \
    || bad "ควรมี cycle ใหม่ (fresh=1) ยาว 2 หนึ่งครั้ง"
DD_TRACE="$TRACE" run_preload 20 ./build/condvar
grep -q ' relx ' "$TRACE" && ok "condvar จด relx (unlock ไม่ตรงเจ้าของ)" || bad "condvar ไม่มี relx"
[ "$(grep -c ' trunc$' "$TRACE")" -le 1 ] && ok "trunc ไม่เกิน 1 บรรทัด" || bad "trunc ซ้ำหลายบรรทัด"

echo "== lockorder (ข้อ 7.1)"
INV='[LOCK ORDER INVERSION]'
run_preload 10 ./build/lockorder
[ "$RC" -eq 0 ] && ok "จบเอง (exit 0)" || bad "ควร exit 0 แต่ได้ $RC"
[ "$(count "$INV")" -eq 1 ] && ok "เตือน $INV หนึ่งครั้ง" || bad "ควรเตือน 1 ครั้ง ได้ $(count "$INV")"
[ "$(count "$MARK")" -eq 0 ] && ok "ไม่มี [DEADLOCK DETECTED] (ยังไม่ค้าง)" || bad "มีรายงาน deadlock ทั้งที่ไม่ค้าง"
[ "$(grep -cE "(locked|then) +mutex 0x[0-9a-f]+ +at lockorder\.c:[0-9]+" "$ERR")" -eq 4 ] \
    && ok "ชี้บรรทัดใน lockorder.c ครบ 4 จุด" || bad "ควรชี้ lockorder.c:<บรรทัด> 4 จุด"
run_preload 15 ./build/no_deadlock
[ "$RC" -eq 0 ] && [ "$(count "$INV")" -eq 0 ] && ok "no_deadlock ล็อกตามลำดับ ไม่เตือน" \
    || bad "no_deadlock ควร exit 0 และไม่เตือน (exit $RC, เตือน $(count "$INV"))"
run_preload 5 ./build/bank
[ "$(count "$MARK 2 threads in cycle")" -eq 1 ] && [ "$(count "$INV")" -eq 0 ] \
    && ok "bank รายงาน deadlock ครั้งเดียว ไม่เตือน inversion ซ้ำ" \
    || bad "bank ควรมี deadlock 1 / inversion 0 (ได้ $(count "$MARK") / $(count "$INV"))"

echo
if [ "$fail" -eq 0 ]; then echo "ผ่านทุกข้อ"; exit 0; else echo "มีข้อที่ไม่ผ่าน"; exit 1; fi
