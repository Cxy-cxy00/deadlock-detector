# Deadlock Detector

ตรวจจับ deadlock ในโปรแกรม multithread บน Linux **โดยไม่ต้องแก้หรือ compile โค้ดของโปรแกรมเป้าหมายใหม่**
ใช้ `LD_PRELOAD` ดักฟังก์ชัน `pthread_mutex_*` แล้วสร้าง wait-for graph เพื่อหา cycle

> วิชา 03603332 Operating Systems · Group Project (5 คน) · หัวข้อจากบท Deadlocks

---

## ปัญหา

โปรแกรมหลาย thread ที่ล็อก mutex สลับลำดับกัน จะค้างทั้งโปรแกรมแบบไม่มีสัญญาณอะไรเลย

```
Thread 1 (โอน A → B)        Thread 2 (โอน B → A)
  lock(mutex A)  ✓            lock(mutex B)  ✓
  lock(mutex B)  ← รอ         lock(mutex A)  ← รอ
```

ต่างคนต่างถือของที่อีกฝ่ายต้องการ ไม่มีใครยอมปล่อย อาการที่เห็นคือ **โปรแกรมเงียบไปเฉย ๆ**
ไม่ crash ไม่มี error CPU เป็น 0% ต้องเดาเองว่าค้างตรงไหน

## สิ่งที่เครื่องมือนี้ทำ

```console
$ ./bank                                 # ค้าง ไม่มีอะไรบอก
^C

$ LD_PRELOAD=./build/libdetect.so ./build/bank

[DEADLOCK DETECTED] 2 threads in cycle

  Thread 17356
      holds   mutex 0x5a8b5c70c030   at bank.c:46
      waiting mutex 0x5a8b5c70c070   at bank.c:51
  Thread 17357
      holds   mutex 0x5a8b5c70c070   at bank.c:70
      waiting mutex 0x5a8b5c70c030   at bank.c:75

  Cycle: T17356 -> T17357 -> T17356
  Hint : acquire locks in a consistent global order
```

| จุดขาย | รายละเอียด |
| --- | --- |
| ไม่ต้องแก้โค้ด | โปรแกรมเป้าหมายไม่ต้องแก้ ไม่ต้อง compile ใหม่ |
| ใช้กับของจริงได้ | ใช้กับ binary ที่คนอื่นเขียนได้ทันที (ขอแค่ลิงก์ `libpthread` แบบ dynamic) |
| ชี้บรรทัดที่ผิด | บอกตำแหน่งใน source ได้ถ้า build ด้วย `-g` |

## มันแทรกตัวอยู่ตรงไหน

```
โปรแกรมเป้าหมาย        pthread_mutex_lock(&A)
        │
        ▼
libdetect.so  ← โปรเจ็คนี้   1. จดว่า "T1 กำลังจะรอ A"
                            2. เรียกตัวจริง
                            3. จดว่า "T1 ได้ A แล้ว"
        │
        ▼
libpthread.so              ตัวจริงที่ทำการล็อก
        │
        ▼
Linux kernel               ไม่แตะส่วนนี้
```

`LD_PRELOAD` สั่งให้ dynamic linker โหลด library ของเราก่อน ฟังก์ชันที่ชื่อซ้ำกันของเราจึงถูกเรียกแทน
และเรายังเรียกตัวจริงต่อได้ผ่าน `dlsym(RTLD_NEXT, ...)`

## ตรวจเจอได้อย่างไร

สร้าง **wait-for graph** (node = thread, edge `T1 → T2` = "T1 รอ mutex ที่ T2 ถืออยู่") แล้วหา cycle ด้วย DFS
มี cycle = เกิด deadlock แล้ว

| เวลา | เกิดอะไร | ตารางของเรา |
| --- | --- | --- |
| t0 | T1 ล็อก A ได้ | `holder[A] = T1` |
| t1 | T2 ล็อก B ได้ | `holder[B] = T2` |
| t2 | T1 ขอ B → ค้าง | edge `T1 → T2` |
| t3 | T2 ขอ A → ค้าง | edge `T2 → T1` |
| t3.5 | detector thread ตื่นมาตรวจ | เจอ cycle → พิมพ์รายงาน |

T1 กับ T2 ค้างอยู่ทั้งคู่ แต่ detector เป็น thread คนละตัวของ library เรา จึงยังทำงานและรายงานออกมาได้

---

## สถานะปัจจุบัน

✅ **ใช้งานได้จริงแล้ว** — `make` ผ่านโดยไม่มี warning และตรวจจับ deadlock ได้พร้อมชี้บรรทัดในซอร์ส

| ส่วน | สถานะ |
| --- | --- |
| ดัก `pthread_mutex_*` ด้วย LD_PRELOAD | ✅ |
| ตารางสถานะ thread / mutex | ✅ |
| wait-for graph + cycle detection (DFS) | ✅ |
| detector thread ตรวจตามคาบ | ✅ |
| รายงานพร้อม `file:line` | ✅ |
| export Graphviz | ✅ |
| โปรแกรมทดสอบ `bank` / `no_deadlock` | ✅ |
| ซิมูเลเตอร์ Banker's Algorithm (`docs/banker.html`) | ✅ |
| หน้าเว็บกดรันโค้ดจริง (`docs/playground.html`) | ✅ |
| เตือน lock-order inversion ล่วงหน้า | 🚧 ยังไม่ได้ทำ |

ลองเองทั้งชุดได้ด้วย `make demo` หรือ `./tests/run_demo.sh`

## โครงสร้างไฟล์

```
src/
  common.h       ค่าคงที่, ชนิดข้อมูลร่วม, dd_logf
  log.c          พิมพ์ข้อความออก stderr โดยไม่ยุ่งกับ stdio ของเป้าหมาย
  interpose.c/h  ดัก pthread_mutex_* ด้วย LD_PRELOAD        — คนที่ 1
  state.c/h      ตารางสถานะ thread / mutex (ใครถือ ใครรอ)   — คนที่ 2
  graph.c/h      wait-for graph + cycle detection (DFS)      — คนที่ 3
  detector.c/h   detector thread + lock-order inversion      — คนที่ 4
  report.c/h     รายงาน + export Graphviz + map file:line    — คนที่ 5
tests/
  bank.c         โปรแกรมโอนเงินที่ deadlock แน่นอน
  no_deadlock.c  เวอร์ชันล็อกตามลำดับ ใช้เช็ค false positive
  run_demo.sh    ลำดับขั้นตอน demo ตอนนำเสนอ
docs/
  overview.html    สไลด์/หน้าสรุปโปรเจ็ค (เปิดในเบราว์เซอร์)
  banker.html      ซิมูเลเตอร์ Banker's Algorithm — deadlock avoidance
  playground.html  หน้าเครื่องมือ — แนบไฟล์ C หรือวางโค้ดแล้วกดรันได้จริง
  design.md        บันทึกการออกแบบและข้อจำกัด
tools/
  playground.py    เซิร์ฟเวอร์ท้องถิ่นที่ compile + รันโค้ดให้หน้า playground
```

## หน้าเว็บที่กดรันโค้ดได้จริง

นอกจากรันในเทอร์มินัล ยังมีหน้าเว็บที่วางโค้ด C ลงไปแล้วกดรันผ่านเครื่องมือได้เลย
เห็นรายงาน เห็น wait-for graph เป็นภาพ และเทียบ "รันแบบปกติ" กับ "รันผ่านเครื่องมือ" ได้ในหน้าเดียว

```bash
make playground
```

แล้วเปิด `http://127.0.0.1:8000`

> **ใช้เฉพาะตอน demo บนเครื่องตัวเอง** — เซิร์ฟเวอร์นี้ compile แล้วรันโค้ด C ที่ส่งมาจากหน้าเว็บ
> ใครที่ต่อเข้าพอร์ตนี้ได้เท่ากับรันอะไรก็ได้บนเครื่องนั้น จึงผูกไว้กับ `127.0.0.1` เท่านั้น
> ห้ามเปิดออกอินเทอร์เน็ต และห้ามรันบนเครื่องที่ใช้ร่วมกับคนอื่น

## Build และใช้งาน

ต้องใช้ **Linux** (WSL2 ใช้ได้) + `gcc` + `make` — `LD_PRELOAD` เป็นกลไกของ glibc dynamic linker จึงใช้บน Windows ตรง ๆ ไม่ได้

```bash
make                 # ได้ build/libdetect.so และโปรแกรมทดสอบ
make tests           # build เฉพาะโปรแกรมทดสอบ
make clean
```

```bash
# รันโปรแกรมใด ๆ ผ่านเครื่องมือ
LD_PRELOAD=./build/libdetect.so ./your_program

# export เป็นภาพ cycle
DD_DOT_OUT=cycle.dot LD_PRELOAD=./build/libdetect.so ./build/bank
dot -Tpng cycle.dot -o cycle.png
```

โปรแกรมเป้าหมายต้อง compile ด้วย `-g` ถ้าอยากให้รายงานชี้เลขบรรทัดได้

## แผนการ demo

1. รันโปรแกรมโอนเงินธรรมดา → ค้างเฉย ๆ ไม่มีอะไรบอก
2. รันใหม่ด้วย `LD_PRELOAD` → รายงานออกมาพร้อมบรรทัดที่ผิด
3. เปิดภาพ cycle ที่ export เป็น Graphviz
4. แก้โค้ดให้ล็อกตามลำดับเดียวกัน → รันใหม่ ไม่มีรายงานแล้ว
5. รันกับโปรแกรมที่ไม่ได้เขียนเอง เพื่อยืนยันว่าใช้กับของจริงได้

## แบ่งงาน

| คน | รับผิดชอบ | หัวข้อที่ present |
| --- | --- | --- |
| 1 | ดักฟังก์ชัน pthread ด้วย LD_PRELOAD | dynamic linking |
| 2 | ตารางสถานะ thread / mutex | process & thread state |
| 3 | wait-for graph + cycle detection | resource allocation graph |
| 4 | lock-order inversion + ชี้บรรทัด | prevention vs detection |
| 5 | โปรแกรมทดสอบ + Graphviz + รายงาน | demo & ข้อจำกัด |

## ข้อจำกัดที่รู้อยู่

- ดักเฉพาะ `pthread_mutex_*` — ยังไม่รองรับ rwlock, condition variable, semaphore
- ใช้ไม่ได้กับ binary ที่ลิงก์ `libpthread` แบบ static
- ตรวจแบบ **detection** (เจอหลังค้างแล้ว) ไม่ใช่ prevention — ไม่ได้กันไม่ให้ค้าง
- แต่ละการล็อกมี overhead เพิ่ม จึงอาจเปลี่ยนจังหวะการทำงานของโปรแกรมที่มี race
- ไม่รองรับ mutex ชนิด `PTHREAD_MUTEX_RECURSIVE` — thread เดิมล็อกตัวเดิมซ้ำจะนับเป็นครั้งเดียว
- จะชี้ `file:line` ได้ต้องมี `addr2line` (แพ็กเกจ binutils) ตอนรัน และโปรแกรมเป้าหมายต้อง build ด้วย `-g`
  ถ้าขาดอย่างใดอย่างหนึ่งจะถอยไปแสดง `at pc 0x...` แทน ยังบอกได้ว่าใครค้างกับใคร
- `LD_PRELOAD` ตกทอดไปยัง process ลูกด้วย จึงอาจได้รายงานจากโปรแกรมที่ไม่ได้ตั้งใจตรวจ
  (เป็นข้อดีถ้าอยากตามทั้ง process tree แต่เป็นกับดักถ้าไม่รู้)

## เนื้อหาในบทเรียนที่ครอบคลุม

เงื่อนไข Coffman · wait-for graph · resource allocation graph · deadlock detection ·
deadlock prevention (lock ordering) · deadlock avoidance (Banker's Algorithm) · safe / unsafe state
