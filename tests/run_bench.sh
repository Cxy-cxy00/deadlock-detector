#!/usr/bin/env bash
# run_bench.sh — วัด overhead มี/ไม่มี LD_PRELOAD แล้วเขียนผลลง tests/logs/bench.txt
# เรียกผ่าน: make bench
set -u
cd "$(dirname "$0")/.." || exit 1

LIB="./build/libdetect.so"     # relative: path เต็มที่มีช่องว่างใช้กับ LD_PRELOAD ไม่ได้
OUT="tests/logs/bench.txt"
mkdir -p tests/logs

median_of() { sed -n 's/.*median=\([0-9.]*\).*/\1/p'; }

{
    echo "# make bench  ($(date -Is))"
    echo "# เครื่องที่วัด"
    echo "cpu    : $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //')"
    echo "cores  : $(nproc)"
    echo "glibc  : $(getconf GNU_LIBC_VERSION)"
    echo "gcc    : $(gcc --version | head -1)"
    echo "kernel : $(uname -r)"
    echo
    echo "# 1,000,000 คู่ lock+unlock ต่อรอบ · วอร์มอัพ 1 รอบ · วัด 15 รอบ · รายงาน median"
} > "$OUT"

fail=0
for mode in uncontended contended; do
    base=$(./build/bench "$mode") || fail=1
    pre=$(env LD_PRELOAD="$LIB" ./build/bench "$mode") || fail=1
    b=$(echo "$base" | median_of); p=$(echo "$pre" | median_of)
    {
        echo
        echo "## $mode"
        echo "ไม่มี LD_PRELOAD : $base"
        echo "มี LD_PRELOAD    : $pre"
        if [ -n "$b" ] && [ -n "$p" ]; then
            awk -v b="$b" -v p="$p" 'BEGIN { printf "overhead         : +%.1f ns/pair (x%.1f)\n", p - b, p / b }'
        fi
    } >> "$OUT"
done

cat "$OUT"
exit "$fail"
