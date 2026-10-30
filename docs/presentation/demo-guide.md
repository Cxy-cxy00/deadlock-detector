# คู่มือ Demo (1:50 นาที · Speaker E พิมพ์และพูด)

มี 2 แบบ เลือกแบบเดียว

| | แบบ A: พิมพ์คำสั่งเอง | แบบ B: ใช้สคริปต์ `run_demo.sh` |
| --- | --- | --- |
| พิมพ์อะไร | 4 คำสั่ง (เตรียมไว้ใน history กด ↑ เรียก) | คำสั่งเดียว แล้วกด Enter ทีละขั้น |
| ข้อความบนจอ | มีแต่ output ของโปรแกรม | สคริปต์พิมพ์คำอธิบาย**ภาษาไทย**คั่นทุกขั้น + `[ผ่าน]` |
| เปิดภาพวง (ขั้น 3) | เปิดเองได้ทันที | ต้องเปิดจากอีกหน้าต่าง |
| เหมาะกับ | present เป็นภาษาอังกฤษ (**แนะนำ**) | ซ้อม หรือกลัวพิมพ์ผิด |

ทั้งสองแบบทดสอบแล้วบน WSL2 Ubuntu เครื่องนี้ — ผลตรงทุกขั้น (ขั้น 1–2 exit 124, ขั้น 2 มีรายงาน, ขั้น 3 ได้ภาพ, ขั้น 4 exit 0 ไม่มีรายงาน)

---

## เตรียมก่อนขึ้นพูด (ทั้งสองแบบ · ทำก่อน 5 นาที)

**1. เปิด Ubuntu (WSL2)** — เลือกวิธีใดวิธีหนึ่ง
- กดปุ่ม **Windows ⊞** → พิมพ์ `Ubuntu` → กด **Enter**
- หรือเปิด **Windows Terminal** → คลิกลูกศร **˅** ข้างปุ่ม **+** → เลือก **Ubuntu**
- หรือใน PowerShell พิมพ์ `wsl` แล้ว Enter

เปิดได้แล้วจะเห็น `frank@ชื่อเครื่อง:~$`

**2. เข้าโฟลเดอร์โปรเจกต์และ build**

```bash
cd ~/dd
```

```bash
make
```

> ต้องใช้ `~/dd` เท่านั้น — เป็นทางลัดไปที่โฟลเดอร์โปรเจกต์ (สร้างไว้แล้ว)
> path จริงมีช่องว่าง (`Bachelor degree`) ถ้าใช้ path จริง LD_PRELOAD จะโหลดไม่ขึ้นแบบเงียบ ๆ แล้ว**ไม่มีรายงาน**
> เช็คด้วย `pwd` ต้องขึ้น `/home/frank/dd`

**3. ขยายฟอนต์** — กด `Ctrl` + `+` หลายครั้ง จนคนหลังห้องอ่านออก

**4. ซ้อมรัน 1 รอบ** (แบบ A: รันทั้ง 4 คำสั่ง · แบบ B: รันสคริปต์) แล้วพิมพ์ `clear` ให้จอว่าง

**5. วางหน้าต่าง** — สลับระหว่างสไลด์กับ terminal ด้วย `Alt` + `Tab`

---

## แบบ A: พิมพ์คำสั่งเอง (แนะนำ)

> ตอนซ้อมพิมพ์ทั้ง 4 คำสั่งไว้แล้ว ตอนจริงกด **↑** จนเจอคำสั่งที่ต้องการแล้ว Enter — ไม่ต้องพิมพ์สด

### ขั้น 1 — โปรแกรมค้างเงียบ ๆ (ประมาณ 25 วินาที)

```bash
timeout 3 ./build/bank; echo "exit=$?"
```

**จะเห็น:** `[t1] lock alice` … `[t1] lock bob ...` แล้วนิ่ง 3 วินาที → `exit=124`

**พูด:**
> This is the bank program, with no tool. It stops. There is no message at all. Exit code 124 means timeout stopped it.

### ขั้น 2 — คำสั่งเดิม เติม LD_PRELOAD (ประมาณ 35 วินาที)

```bash
timeout 3 env LD_PRELOAD=./build/libdetect.so ./build/bank; echo "exit=$?"
```

**จะเห็น:**

```
[DEADLOCK DETECTED] 2 threads in cycle

  Thread 686
      holds   mutex 0x...   at bank.c:89
      waiting mutex 0x...   at bank.c:94
  Thread 685
      holds   mutex 0x...   at bank.c:65
      waiting mutex 0x...   at bank.c:70

  Cycle: T686 -> T685 -> T686
  Hint : acquire locks in a consistent global order
exit=124
```

(เลข thread และที่อยู่ mutex เปลี่ยนทุกครั้งที่รัน — ปกติ · เลขบรรทัด 65 / 70 / 89 / 94 ต้องตรงเสมอ)

**พูด:**
> Same command. We only add LD_PRELOAD. Now we get a report. Look at lines 65, 70, 89 and 94.

### ขั้น 3 — วาดภาพวง (ประมาณ 25 วินาที · **ถ้าเวลาเกินให้ข้าม**)

```bash
DD_DOT_OUT=cycle.dot timeout 3 env LD_PRELOAD=./build/libdetect.so ./build/bank 2>/dev/null; dot -Tpng cycle.dot -o cycle.png; explorer.exe cycle.png
```

**จะเห็น:** หน้าต่างรูปของ Windows เปิดขึ้น — วงรี = thread, สี่เหลี่ยม = mutex, ลูกศรแดง = กำลังรอ วนครบรอบ
(ดูเสร็จปิดหน้าต่างรูป แล้ว `Alt` + `Tab` กลับ terminal)

**พูด:**
> We can save the loop as a picture. Red arrows mean waiting.

### ขั้น 4 — เวอร์ชันที่แก้แล้ว (ประมาณ 25 วินาที)

```bash
timeout 15 env LD_PRELOAD=./build/libdetect.so ./build/no_deadlock; echo "exit=$?"
```

**จะเห็น:** รันราว 2 วินาที → `PASS: ...` → `exit=0` ไม่มี `[DEADLOCK DETECTED]`

**พูด:**
> This version locks in the same order. It always locks Alice before Bob. It finishes with exit code 0. There is no report. So there is no false alarm.

จบแล้วกลับไปสไลด์ 13 ให้ Speaker D พูดผลลัพธ์ต่อ

---

## แบบ B: ใช้สคริปต์ `run_demo.sh`

### คำสั่งเดียว

```bash
bash tests/run_demo.sh
```

สคริปต์จะ `make` ให้ก่อน แล้วหยุดรอ **"[กด Enter เพื่อไปต่อ]"** ระหว่างแต่ละขั้น
พูดตามบทของแต่ละขั้นจบแล้วค่อยกด **Enter**

| ขั้น | บนจอขึ้นหัวว่า | จะเห็น | พูด (เหมือนแบบ A) | เช็คว่าผ่าน |
| --- | --- | --- | --- | --- |
| 1 | `1) รันโปรแกรมโอนเงินธรรมดา` | `[t1] lock alice` … นิ่ง 3 วินาที | This is the bank program, with no tool… | `[ผ่าน] ค้างจนหมดเวลา (exit 124)` |
| 2 | `2) คำสั่งเดิมเป๊ะ แค่เติม LD_PRELOAD ข้างหน้า` | `[DEADLOCK DETECTED]` + บรรทัด 65/70/89/94 และสคริปต์ `grep` บรรทัด `pthread_mutex_lock` ใน `bank.c` มาวางให้เทียบ | Same command. We only add LD_PRELOAD… (ชี้บรรทัดที่สคริปต์แสดงเทียบได้เลย) | `[ผ่าน] stderr มี [DEADLOCK DETECTED]` |
| 3 | `3) export wait-for graph เป็นภาพ` | เนื้อไฟล์ `cycle.dot` (ตัวหนังสือ) + `-> เขียน cycle.png แล้ว` | We can save the loop as a picture… | `[ผ่าน] ได้ไฟล์ cycle.dot และไม่ว่าง` |
| 4 | `4) เวอร์ชันที่แก้แล้ว — ล็อกตามลำดับสากล` | รัน ~2 วินาที → `PASS` | This version locks in the same order… | `[ผ่าน] จบเองไม่ค้าง (exit 0)` |

จบจะขึ้น **"ผ่านทุกขั้น"**

### ข้อควรรู้ของแบบ B
- **ข้อความคำอธิบายบนจอเป็นภาษาไทย** — ถ้าผู้ฟังเป็นต่างชาติให้พูดอังกฤษทับ ไม่ต้องอ่านตามจอ
- **ขั้น 3 สคริปต์ไม่เปิดรูปให้** — แสดงแค่ข้อความในไฟล์ .dot ถ้าอยากโชว์รูป ให้เปิด Ubuntu อีกหน้าต่างไว้ก่อน แล้วพิมพ์
  ```bash
  cd ~/dd && explorer.exe cycle.png
  ```
  (ต้องรันหลังสคริปต์ผ่านขั้น 3 แล้ว ไฟล์ถึงจะมี) · หรือข้ามแล้วชี้ภาพในสไลด์ 10 แทน
- ซ้อมจับเวลาแบบไม่ต้องกด Enter: `bash tests/run_demo.sh --no-pause` (ต้องจบด้วย `ผ่านทุกขั้น` และ exit 0)

---

## ถ้าพลาด

| อาการ | แก้ |
| --- | --- |
| ขั้น 2 ไม่มีรายงาน | ไม่ได้อยู่ใน `~/dd` → พิมพ์ `cd ~/dd` แล้วรันใหม่ (เช็คด้วย `pwd`) |
| `No such file or directory: ./build/bank` | ยังไม่ได้ build → พิมพ์ `make` |
| `dot: command not found` | ข้ามขั้น 3 แล้วชี้ภาพในสไลด์ 10 |
| รูปไม่เปิด | ข้ามได้ ชี้ภาพในสไลด์ 10 |
| อะไรก็ตามพังกลางทาง | กลับไปสไลด์ 10 ซึ่งมี output จริงอยู่ แล้วอธิบายจากภาพนั้น (อัดวิดีโอ demo สำรองไว้ด้วยจะดีมาก) |
| เวลาเกิน | ข้ามขั้น 3 ได้คืนราว 25 วินาที |

## ถ้าใช้เครื่องอื่นตอน present (ทำครั้งเดียว)

```bash
sudo apt install build-essential graphviz
```

```bash
ln -s "<path เต็มของโฟลเดอร์ deadlock-detector>" ~/dd
```

```bash
cd ~/dd && make && bash tests/run_demo.sh --no-pause
```

ต้องจบด้วย `ผ่านทุกขั้น` ก่อนใช้จริง
