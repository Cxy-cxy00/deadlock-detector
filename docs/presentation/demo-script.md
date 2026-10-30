# Demo Script — Speaker E (1:50)

ก่อนเริ่ม: เปิด Ubuntu → `cd ~/dd` → `make` → ซ้อมทุกคำสั่ง 1 รอบ → `clear`
ตอนจริงกด ↑ เรียกคำสั่งเดิม ไม่ต้องพิมพ์สด

---

## Step 1 — the program just stops

```bash
timeout 3 ./build/bank; echo "exit=$?"
```

> This is the bank program, with no tool.
> It just hangs.
> There is no message at all.
> Exit code 124 means timeout stopped it.

---

## Step 2 — same command, add LD_PRELOAD

```bash
timeout 3 env LD_PRELOAD=./build/libdetect.so ./build/bank; echo "exit=$?"
```

> Same command.
> We only add LD_PRELOAD.
> It still hangs, but now we know why.
> Look at lines 65, 70, 89 and 94.

---

## Step 3 — picture of the loop (ข้ามได้ถ้าเวลาเกิน)

```bash
DD_DOT_OUT=cycle.dot timeout 3 env LD_PRELOAD=./build/libdetect.so ./build/bank 2>/dev/null; dot -Tpng cycle.dot -o cycle.png; explorer.exe cycle.png
```

> We can save the loop as a picture.
> Red arrows mean waiting.

---

## Step 4 — the fixed version

```bash
timeout 15 env LD_PRELOAD=./build/libdetect.so ./build/no_deadlock; echo "exit=$?"
```

> Now, the fixed version.
> This version locks in the same order.
> It always locks Alice before Bob.
> It finishes with exit code 0.
> There is no report.
> So there is no false alarm.

---

ส่งต่อ Speaker D (นายธรรศธรณ์ เจียมตน · สไลด์ 13):

> Now Thatthorn will show our results.
