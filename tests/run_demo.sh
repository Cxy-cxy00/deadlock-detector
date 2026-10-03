#!/usr/bin/env bash
# run_demo.sh — ลำดับการ demo ตอนนำเสนอ (ดู docs/overview.html หัวข้อ 5)
set -u
cd "$(dirname "$0")/.." || exit 1

echo "== 1) รันธรรมดา: ค้างเฉย ๆ ไม่มีอะไรบอก (กด Ctrl-C) =="
echo "   ./build/bank"
echo
echo "== 2) รันผ่านเครื่องมือ: ได้รายงานพร้อมบรรทัดที่ผิด =="
echo "   LD_PRELOAD=./build/libdetect.so ./build/bank"
echo
echo "== 3) เปิดภาพ cycle ที่ export เป็น Graphviz =="
echo "   DD_DOT_OUT=cycle.dot LD_PRELOAD=./build/libdetect.so ./build/bank"
echo "   dot -Tpng cycle.dot -o cycle.png"
echo
echo "== 4) เวอร์ชันที่แก้แล้ว: ต้องไม่มีรายงาน =="
echo "   LD_PRELOAD=./build/libdetect.so ./build/no_deadlock"
echo
echo "TODO: เปลี่ยนจาก echo เป็นรันจริงเมื่อ build ผ่านแล้ว"
