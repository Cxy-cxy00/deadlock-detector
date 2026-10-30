# Deadlock Detector

ตรวจจับ deadlock ในโปรแกรม multithread บน Linux **โดยไม่ต้องแก้หรือ compile โค้ดของโปรแกรมเป้าหมายใหม่**
ใช้ `LD_PRELOAD` interpose `pthread_mutex_lock` / `pthread_mutex_trylock` / `pthread_mutex_unlock`
แล้วสร้าง wait-for graph เพื่อหา cycle

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

output จริงจาก [`tests/logs/phase2_bank_preload.txt`](tests/logs/phase2_bank_preload.txt)
(glibc 2.43 · gcc 15.2.0 · WSL2 kernel 6.6 — ดู [`tests/logs/env.txt`](tests/logs/env.txt))

```console
$ timeout 5s ./build/bank                                        # ค้าง ไม่มีอะไรบอก (exit 124)

$ timeout 5s env LD_PRELOAD="$PWD/build/libdetect.so" ./build/bank

[DEADLOCK DETECTED] 2 threads in cycle

  Thread 514
      holds   mutex 0x56fc6b20b070   at bank.c:89
      waiting mutex 0x56fc6b20b030   at bank.c:94
  Thread 513
      holds   mutex 0x56fc6b20b030   at bank.c:65
      waiting mutex 0x56fc6b20b070   at bank.c:70

  Cycle: T514 -> T513 -> T514
  Hint : acquire locks in a consistent global order
```

บรรทัด 65/70/89/94 คือ `pthread_mutex_lock` สี่จุดใน [`tests/bank.c`](tests/bank.c) ตรงตัว

| จุดขาย | รายละเอียด |
| --- | --- |
| ไม่ต้องแก้โค้ด | โปรแกรมเป้าหมายไม่ต้องแก้ ไม่ต้อง compile ใหม่ |
| ใช้กับ binary อื่นได้ | Linux/glibc dynamic binary · glibc ≥ 2.34 ไม่ต้องมี libpthread แยก · ผ่าน smoke test กับโปรแกรมอื่น (`ls`, `git`, `make`, `gcc`) แต่ยังไม่รับรองทุกแอป |
| ชี้บรรทัดที่ผิด | บอกตำแหน่งใน source ได้ถ้าโปรแกรมเป้าหมาย build ด้วย `-g` เป็น PIE และมี `addr2line` |
| เตือนก่อนค้าง | สอง mutex ถูกล็อกสลับลำดับกัน → `[LOCK ORDER INVERSION]` ทั้งที่โปรแกรมยังไม่ค้าง ([ดูด้านล่าง](#เตือนก่อนค้าง-lock-order-inversion)) |

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
libc.so.6                  ตัวจริงที่ทำการล็อก (glibc ≥ 2.34 รวม libpthread ไว้ในนี้แล้ว)
        │
        ▼
Linux kernel               ไม่แตะส่วนนี้
```

`LD_PRELOAD` สั่งให้ dynamic linker โหลด library ของเราก่อน ฟังก์ชันที่ชื่อซ้ำกันของเราจึงถูกเรียกแทน
และเรายังเรียกตัวจริงต่อได้ผ่าน `dlsym(RTLD_NEXT, ...)` ถ้าหาตัวจริงไม่เจอ library จะ `abort()` พร้อมบอกเหตุผล
(ไม่ทำเป็นว่าล็อกสำเร็จ เพราะนั่นทำให้ mutual exclusion เสียแบบเงียบ ๆ)

## ตรวจเจอได้อย่างไร

สร้าง **wait-for graph** (node = thread, edge `T1 → T2` = "T1 รอ mutex ที่ T2 ถืออยู่") แล้วหา cycle ด้วย DFS
cycle = circular wait ภายใต้ scope ที่รองรับ (NORMAL mutex ที่ล็อกด้วย `pthread_mutex_lock` — ดู [Limitations](#limitations))

| เวลา | เกิดอะไร | ตารางของเรา |
| --- | --- | --- |
| t0 | T1 ล็อก A ได้ | `holder[A] = T1` |
| t1 | T2 ล็อก B ได้ | `holder[B] = T2` |
| t2 | T1 ขอ B → ค้าง | edge `T1 → T2` |
| t3 | T2 ขอ A → ค้าง | edge `T2 → T1` |
| ≤ 500 ms ถัดมา | detector thread ตื่นมาตรวจตามคาบ | เจอ cycle → พิมพ์รายงาน |

T1 กับ T2 ค้างอยู่ทั้งคู่ แต่ detector เป็น thread คนละตัวของ library เรา จึงยังทำงานและรายงานออกมาได้

## เตือนก่อนค้าง: lock-order inversion

ทุกครั้งที่ thread จะรอ mutex ตัวใหม่ขณะถือตัวอื่นอยู่ library จดคู่ลำดับไว้ (ถือ A แล้วขอ B)
ถ้าวันหนึ่งเจอคู่กลับทาง (ถือ B แล้วขอ A) แปลว่าโปรแกรม **ยังไม่ค้างก็จริง แต่ค้างได้** ถ้าสองทางนี้วิ่งพร้อมกัน
output จริงจาก [`tests/logs/phase7_lockorder.txt`](tests/logs/phase7_lockorder.txt)
([`tests/lockorder.c`](tests/lockorder.c) ล็อก a→b แล้วค่อย b→a คนละช่วงเวลา จึงจบเองทุกครั้ง)

```console
$ timeout 10 env LD_PRELOAD=./build/libdetect.so ./build/lockorder     # exit 0

[LOCK ORDER INVERSION] 2 mutexes locked in opposite orders

  Order 1 (thread 924): mutex 0x5efbf1557060 -> mutex 0x5efbf15570a0
      locked  mutex 0x5efbf1557060   at lockorder.c:31
      then    mutex 0x5efbf15570a0   at lockorder.c:32
  Order 2 (thread 925): mutex 0x5efbf15570a0 -> mutex 0x5efbf1557060
      locked  mutex 0x5efbf15570a0   at lockorder.c:42
      then    mutex 0x5efbf1557060   at lockorder.c:43

  Risk : no deadlock yet — if both orders run at the same time, they will deadlock
  Hint : acquire locks in a consistent global order
```

- จดตอน "จะรอ" (ใน `pthread_mutex_lock`) ไม่ใช่ตอนได้ lock — ตอน deadlock จริง lock ตัวที่สองไม่มีวันได้มา
- ถ้าคู่นั้นค้างเป็น deadlock ไปแล้ว (เช่น `bank`) จะเห็นแค่ `[DEADLOCK DETECTED]` ไม่เตือนซ้ำ
- โปรแกรมที่จบเร็วกว่าคาบของ detector ยังได้คำเตือน เพราะ library พิมพ์คิวที่ค้างตอนปิดตัว

---

## Scope ที่ทำจริง

| ส่วน | สถานะ |
| --- | --- |
| interpose `pthread_mutex_lock` / `trylock` / `unlock` ด้วย LD_PRELOAD | ✅ |
| ตารางสถานะ thread / mutex (array ขนาดคงที่ ไม่ malloc ใน lock path) | ✅ |
| robust mutex: `EOWNERDEAD` นับว่าได้ lock · unlock โดยคนที่ไม่ใช่เจ้าของ (EPERM) ไม่ลบเจ้าของเดิม | ✅ เฉพาะ 2 กรณีนี้ |
| wait-for graph + cycle detection (DFS) | ✅ |
| detector thread ตรวจทุก 500 ms | ✅ |
| รายงาน `file:line` (PIE + `-g` + `addr2line`) | ✅ |
| cycle ยาวเกิน 64 thread: รายงานเป็น partial path ไม่ปิดวงปลอม | ✅ |
| export Graphviz (`DD_DOT_OUT`) | ✅ |
| บันทึกเหตุการณ์ทีละขั้น (`DD_TRACE`) + timeline ในหน้า playground | ✅ (เก็บ 5,000 เหตุการณ์ lock แรก) |
| หลาย cycle พร้อมกัน: รายงานทุกวงในการตรวจรอบเดียว วงละครั้ง · `.dot` รวมทุกวง | ✅ (สูงสุด 16 วง) |
| โปรแกรมทดสอบ `bank` / `no_deadlock` / `sample_webapp` / `two_pairs` / `cycle3` / `lockorder` | ✅ |
| regression test `make regress` (robust / errcheck / longcycle / condvar / lockorder) | ✅ |
| `tests/run_demo.sh` ตรวจผลจริงทุกขั้น (exit 0 = ผ่านหมด) | ✅ |
| ซิมูเลเตอร์ Banker's Algorithm (`docs/banker.html`) | ✅ |
| หน้าเว็บกดรันโค้ดจริง (`docs/playground.html`) | ✅ |
| เตือน lock-order inversion ล่วงหน้า (`[LOCK ORDER INVERSION]`) | ✅ เฉพาะคู่ 2 mutex |

## โครงสร้างไฟล์

```
src/
  common.h       ค่าคงที่, ชนิดข้อมูลร่วม, dd_logf
  log.c          พิมพ์ข้อความออก stderr โดยไม่ยุ่งกับ stdio ของเป้าหมาย
  interpose.c/h  interpose pthread_mutex_lock/trylock/unlock    — คนที่ 1
  state.c/h      ตารางสถานะ thread / mutex (ใครถือ ใครรอ) + คู่ลำดับการล็อก — คนที่ 2
  graph.c/h      wait-for graph + cycle detection (DFS)        — คนที่ 3
  detector.c/h   detector thread + พิมพ์คำเตือน lock-order inversion — คนที่ 4
  report.c/h     รายงาน + export Graphviz + map file:line      — คนที่ 5
  trace.c/h      จดเหตุการณ์ทีละขั้นลงไฟล์ (DD_TRACE) ให้หน้า playground เล่นเป็น timeline
tests/
  bank.c         โปรแกรมโอนเงินที่ deadlock ทุกครั้ง — pthread_barrier บังคับให้ทั้งสอง thread
                 ถือ lock ตัวแรกครบก่อนไปขอตัวที่สอง (ไม่ได้พึ่งจังหวะ sleep)
  no_deadlock.c  เวอร์ชันล็อกตามลำดับ id รันนาน ≥ 3 คาบของ detector ใช้เช็ค false positive
  sample_webapp.c ตัวอย่างหน้าตาเหมือนโค้ดจริง (pool/stats ล็อกสวนกัน) ใช้ barrier เช่นกัน
  two_pairs.c    deadlock 2 วงพร้อมกัน — ต้องรายงานครบทั้งสองวง
  cycle3.c       deadlock 3 thread วนเป็นวง a -> b -> c -> a (barrier ให้เกิดทุกครั้ง)
  condvar.c      สอง thread ผลัดกันด้วย condvar ไม่มี deadlock — ต้องไม่มีรายงาน และเตือนแค่ครั้งเดียว
  robust.c       robust mutex ที่เจ้าของตาย (EOWNERDEAD) แล้ว deadlock ผ่านตัวนั้น — ต้องรายงาน
  errcheck.c     unlock โดยคนที่ไม่ใช่เจ้าของ (EPERM) แล้ว deadlock ผ่านตัวนั้น — ต้องรายงาน
  longcycle.c    วงยาว 70 thread — ต้องรายงานเป็น partial path ไม่ปิดวงปลอม
  lockorder.c    ล็อก a->b แล้วค่อย b->a คนละช่วงเวลา ไม่ค้าง — ต้องเตือน lock-order inversion ครั้งเดียว
  bench.c        วัด overhead (make bench -> tests/logs/bench.txt)
  run_demo.sh    ลำดับขั้นตอน demo ตอนนำเสนอ พร้อมเงื่อนไขผ่าน/ไม่ผ่าน
  run_regress.sh regression test ของกรณีขอบ (robust / errcheck / longcycle / condvar / trace / lockorder) — make regress
  logs/          ผลรันจริงที่อ้างใน README (env, test matrix, demo)
docs/
  overview.html    สไลด์/หน้าสรุปโปรเจ็ค (เปิดในเบราว์เซอร์)
  banker.html      ซิมูเลเตอร์ Banker's Algorithm — deadlock avoidance
  playground.html  หน้าเครื่องมือ — แนบไฟล์ C หรือวางโค้ดแล้วกดรันได้จริง
  design.md        บันทึกการออกแบบและข้อจำกัด
  fix-report.md    รายงานการแก้ Phase 0-7: เจออะไร ทำอะไร หลักฐานอยู่ไหน
  presentation/    สไลด์นำเสนอภาษาอังกฤษ (HTML + PDF) + บทพูด + แผนนำเสนอ
tools/
  playground.py    เซิร์ฟเวอร์ท้องถิ่นที่ compile + รันโค้ดให้หน้า playground
```

## แผนที่โค้ด: แต่ละขั้นอยู่ฟังก์ชันไหน

ใช้ตอบคำถามว่า "ส่วนนี้อยู่ตรงไหนในโค้ด" เรียงตาม 4 ขั้นในสไลด์ (catch → record → check → report)
ให้ยึดชื่อฟังก์ชันเป็นหลัก ส่วนเลขบรรทัดอาจเลื่อนได้ถ้ามีการแก้โค้ดภายหลัง

| ขั้น | ทำอะไร | ฟังก์ชัน |
|---|---|---|
| 1 catch | หาฟังก์ชันตัวจริงด้วย `dlsym(RTLD_NEXT)` หาไม่เจอ → `abort()` | [`resolve_one`](src/interpose.c#L83), [`dd_interpose_resolve`](src/interpose.c#L98) |
| | wrapper ของ lock: `wait_begin` → ล็อกจริง (deadlock ค้างที่ [บรรทัดนี้](src/interpose.c#L167)) → `wait_end` → `acquired` | [`pthread_mutex_lock`](src/interpose.c#L152) |
| | wrapper ของ trylock / unlock (unlock จด `released` **ก่อน** ปล่อยจริง) | [`pthread_mutex_trylock`](src/interpose.c#L180), [`pthread_mutex_unlock`](src/interpose.c#L200) |
| | เก็บ PC ของจุดที่เรียกล็อกด้วย `__builtin_return_address(0)` | [interpose.c:153](src/interpose.c#L153) |
| | ธง `in_dd` กันโค้ดของเราวนกลับเข้า wrapper | [interpose.c:42](src/interpose.c#L42) |
| 2 record | ขนาดตารางคงที่ (`DD_MAX_THREADS`, `DD_MAX_MUTEXES`) และคาบตรวจ `DD_CHECK_INTERVAL_MS` | [common.h](src/common.h#L10) |
| | เริ่มรอ + จดคู่ลำดับการล็อก (ใช้เตือน lock-order inversion) | [`dd_state_wait_begin`](src/state.c#L210), [`note_order`](src/state.c#L124) |
| | เลิกรอ / ได้ล็อก / ปล่อยล็อก | [`dd_state_wait_end`](src/state.c#L240), [`dd_state_acquired`](src/state.c#L253), [`dd_state_released`](src/state.c#L275) |
| 3 check | constructor สร้าง detector thread ตอนโหลด library | [`dd_startup`](src/detector.c#L176), [`dd_detector_start`](src/detector.c#L156) |
| | ลูปตื่นทุก 500 ms — บรรทัดแรกตั้ง `in_dd` ค้าง จึงไม่โผล่ในกราฟ | [`detector_main`](src/detector.c#L134) |
| | หนึ่งรอบตรวจ: หยิบคิว inversion → สร้างกราฟ → หา cycle → รายงานเฉพาะวงใหม่ | [`dd_detector_check_once`](src/detector.c#L98) |
| | สร้างลูกศร waiter → holder จาก snapshot ของตาราง | [`dd_graph_build`](src/graph.c#L62) |
| | DFS สามสี (WHITE/GRAY/BLACK) เดินตามโซ่ เจอ GRAY = cycle, เกิน 64 ตั้ง `partial` | [`dd_graph_find_cycles`](src/graph.c#L107) |
| 4 report | พิมพ์ `[DEADLOCK DETECTED]` | [`dd_report_cycle`](src/report.c#L153) |
| | แปลง PC เป็น `file:line` ด้วย `dladdr` + `addr2line` (ลบ 1 จาก PC ที่ [บรรทัดนี้](src/report.c#L101)) | [`dd_report_resolve_site`](src/report.c#L79) |
| | พิมพ์ `[LOCK ORDER INVERSION]` / เขียนไฟล์ DOT | [`dd_report_inversion`](src/report.c#L237), [`dd_report_dot`](src/report.c#L338) |

กรณีขอบที่ `make regress` ทดสอบ แต่ละเทสชี้ไปที่โค้ดที่แก้ปัญหานั้น (ถ้าลบโค้ดส่วนนั้นออก เทสจะไม่ผ่าน):

| เทส | ปัญหา | โค้ดที่รับมือ |
|---|---|---|
| `robust` | เจ้าของตายขณะถือล็อก → คืน `EOWNERDEAD` แต่ถือว่าได้ล็อกแล้ว | [`lock_was_acquired`](src/interpose.c#L148) |
| `errcheck` | คนที่ไม่ใช่เจ้าของสั่ง unlock → คงเจ้าของเดิมไว้ในตาราง | [`dd_state_released`](src/state.c#L279) |
| `longcycle` | วงยาว 70 เกินที่เก็บได้ → รายงานเป็น partial ไม่วาดลูกศรปิดวง | [graph.c:150](src/graph.c#L150), [report.c:183](src/report.c#L183) |
| `lockorder` | ล็อกสลับลำดับแต่ยังไม่ค้าง → เตือนครั้งเดียว และไม่เตือนซ้ำถ้าคู่นั้นอยู่ใน deadlock แล้ว | [`note_order`](src/state.c#L124), [`covered_by_cycle`](src/detector.c#L78) |
| `condvar` | `pthread_cond_wait` ล็อกคืนโดยไม่ผ่าน wrapper → เตือนครั้งเดียว ไม่รายงาน deadlock ปลอม | [`dd_state_released`](src/state.c#L275) |

## หน้าเว็บที่กดรันโค้ดได้จริง

วางโค้ด C แล้วกดรันผ่านเครื่องมือ เห็นรายงาน เห็น wait-for graph เป็นภาพ
และเทียบ "รันแบบปกติ" กับ "รันผ่านเครื่องมือ" ได้ในหน้าเดียว

แผง **การทำงานทีละขั้น** เล่นย้อนเหตุการณ์จริงที่ library จดไว้ระหว่างรัน (`DD_TRACE`):
ใครล็อกอะไรได้ ใครต้องรอใคร edge ใน wait-for graph เกิดตอนไหน และ detector ตื่นมาตรวจกี่รอบก่อนเจอวง
แต่ละขั้นแสดงภาพกราฟ ตาราง holds / waits ของเครื่องมือ ณ ตอนนั้น และไฮไลต์บรรทัดในโค้ด
กดเล่นอัตโนมัติ เลื่อนทีละขั้นด้วย ← → หรือกด "ไปตอนเจอวง" ได้

```bash
make playground
```

แล้วเปิด `http://127.0.0.1:8000`  ถ้า path ของโปรเจกต์มีช่องว่าง เซิร์ฟเวอร์จะแจ้ง error แทนการรัน (ดู Limitations)

> **ใช้เฉพาะตอน demo บนเครื่องตัวเอง** — เซิร์ฟเวอร์นี้ compile แล้วรันโค้ด C ที่ส่งมาจากหน้าเว็บ
> ใครที่ต่อเข้าพอร์ตนี้ได้เท่ากับรันอะไรก็ได้บนเครื่องนั้น จึงผูกไว้กับ `127.0.0.1` เท่านั้น
> ห้ามเปิดออกอินเทอร์เน็ต และห้ามรันบนเครื่องที่ใช้ร่วมกับคนอื่น

## Build และใช้งาน

ต้องใช้ **Linux** (WSL2 ใช้ได้) + `gcc` + `make` + `binutils` (`addr2line`) — `LD_PRELOAD` เป็นกลไกของ
glibc dynamic linker จึงใช้บน Windows ตรง ๆ ไม่ได้

```bash
make                 # ได้ build/libdetect.so และโปรแกรมทดสอบ
make tests           # build เฉพาะโปรแกรมทดสอบ
make bench           # วัด overhead (ผลเขียนลง tests/logs/bench.txt)
make regress         # regression test ของกรณีขอบที่เคยแก้ (exit 0 = ผ่านหมด)
make clean
```

```bash
# รันโปรแกรมใด ๆ ผ่านเครื่องมือ (path ของ .so ต้องไม่มีช่องว่าง)
LD_PRELOAD=./build/libdetect.so ./your_program

# export เป็นภาพ cycle
DD_DOT_OUT=cycle.dot LD_PRELOAD=./build/libdetect.so ./build/bank
dot -Tpng cycle.dot -o cycle.png

# จดเหตุการณ์ทีละขั้น (wait / acq / rel / รอบตรวจของ detector) — รูปแบบไฟล์อยู่ที่หัว src/trace.h
DD_TRACE=trace.txt LD_PRELOAD=./build/libdetect.so ./build/bank
```

โปรแกรมเป้าหมายต้อง compile ด้วย `-g` ถ้าอยากให้รายงานชี้เลขบรรทัดได้

## แผนการ demo

```bash
bash tests/run_demo.sh            # หยุดรอ Enter ทีละขั้น
bash tests/run_demo.sh --no-pause # รันรวดเดียว — exit 0 เมื่อผ่านทุกขั้น
```

1. รันโปรแกรมโอนเงินธรรมดา → ค้างจนหมดเวลา (ต้องได้ exit 124)
2. รันใหม่ด้วย `LD_PRELOAD` → ยังค้าง (exit 124) และ stderr ต้องมี `[DEADLOCK DETECTED]` พร้อมบรรทัด
3. export Graphviz → `cycle.dot` ต้องมีและไม่ว่าง
4. เวอร์ชันล็อกตามลำดับ → ต้อง exit 0 และไม่มีรายงาน

`--smoke` เพิ่มขั้น 5: โหลดกับโปรแกรมอื่น (`ls`, `git`, `make`, `gcc`) ได้โดยไม่พัง
— ยืนยันแค่ว่าโหลดเข้าไปแล้วโปรแกรมยังทำงานปกติ ไม่ได้พิสูจน์ว่าตรวจ deadlock ในโปรแกรมเหล่านั้นได้

## ผลทดสอบ

เครื่องที่ทดสอบ: glibc 2.43 · gcc 15.2.0 (Ubuntu 26.04) · WSL2 kernel 6.6.114.1 · x86-64
([`tests/logs/env.txt`](tests/logs/env.txt))

| โปรแกรม | ไม่มี LD_PRELOAD | มี LD_PRELOAD |
| --- | --- | --- |
| `bank` | ค้าง exit 124 | exit 124 + `[DEADLOCK DETECTED]` |
| `no_deadlock` | exit 0 | exit 0 ไม่มีรายงาน (detector ตรวจไป 3 รอบระหว่างรัน) |
| `sample_webapp` | ค้าง exit 124 | exit 124 + `[DEADLOCK DETECTED]` |
| `cycle3` (วงยาว 3) | ค้าง exit 124 | exit 124 + `3 threads in cycle` (20/20 รอบ, [`phase3_cycle3.txt`](tests/logs/phase3_cycle3.txt)) |
| `two_pairs` (2 วงพร้อมกัน) | ค้าง exit 124 | exit 124 + รายงาน 2 ครั้ง วงละครั้ง (10/10 รอบ, [`phase3_two_pairs.txt`](tests/logs/phase3_two_pairs.txt)) |
| `lockorder` (สลับลำดับแต่ไม่ค้าง) | exit 0 | exit 0 + `[LOCK ORDER INVERSION]` 1 ครั้ง ([`phase7_lockorder.txt`](tests/logs/phase7_lockorder.txt)) |

- ความแน่นอน: `bank` แบบ preload ได้รายงาน **20/20** รอบ, `sample_webapp` **20/20** รอบ
  ([`phase1_*_preload_x20.txt`](tests/logs/))
- `run_demo.sh --no-pause` ผ่านทุกขั้น และคืน exit 1 จริงเมื่อชี้ไปที่ .so ที่ไม่มีอยู่
  ([`phase2_run_demo_*.txt`](tests/logs/))
- cycle 70 thread รายงานเป็น partial path ไม่ปิดวงปลอม ([`phase2_partial_cycle.txt`](tests/logs/phase2_partial_cycle.txt))
- `make regress` ผ่านทุกข้อ (robust / errcheck / longcycle / condvar / lockorder) และแต่ละข้อ **ไม่ผ่าน**
  เมื่อถอย fix ของข้อนั้นออกชั่วคราว = เทสจับบั๊กได้จริง ([`phase5_regress.txt`](tests/logs/phase5_regress.txt),
  [`phase7_regress.txt`](tests/logs/phase7_regress.txt), [`phase7_lockorder.txt`](tests/logs/phase7_lockorder.txt))
- lock-order inversion ไม่เตือนซ้ำในโปรแกรมที่ค้างจริง: `bank` 10/10 รอบได้ `[DEADLOCK DETECTED]` อย่างเดียว
  และ `no_deadlock` / `condvar` ไม่มีคำเตือน ([`phase7_lockorder.txt`](tests/logs/phase7_lockorder.txt))
- overhead (`make bench`, i9-13900H บน WSL2, median 15 รอบ × 1,000,000 คู่ lock+unlock —
  [`bench.txt`](tests/logs/bench.txt))

  | กรณี | ไม่มี LD_PRELOAD | มี LD_PRELOAD | เพิ่มขึ้น |
  | --- | --- | --- | --- |
  | uncontended (1 thread) | 2.3 ns/คู่ | 72.1 ns/คู่ | +69.8 ns (×31.3) |
  | contended (4 thread, 1 mutex) | 39.9 ns/คู่ | 313.5 ns/คู่ | +273.6 ns (×7.9) |

  ทุก lock/unlock ต้องผ่านตารางที่มี lock ตัวเดียว จึงช้าลงชัดเจน เหมาะกับการ debug ไม่ใช่เปิดทิ้งไว้ใน production
  การจดลำดับการล็อก (Phase 7) เพิ่มราว 1 ns กรณี 1 thread และราว 5% กรณี contended
  เทียบ A/B ในรอบเดียวกัน ([`phase7_bench_ab.txt`](tests/logs/phase7_bench_ab.txt))

## แบ่งงาน

| คน | รับผิดชอบ | หัวข้อที่ present |
| --- | --- | --- |
| 1 | interpose ฟังก์ชัน pthread ด้วย LD_PRELOAD | dynamic linking |
| 2 | ตารางสถานะ thread / mutex | process & thread state |
| 3 | wait-for graph + cycle detection | resource allocation graph |
| 4 | detector thread + ชี้บรรทัด + เตือน lock-order inversion | prevention vs detection |
| 5 | โปรแกรมทดสอบ + Graphviz + รายงาน | demo & ข้อจำกัด |

## Limitations

- **NORMAL mutex เท่านั้น** — interpose แค่ `pthread_mutex_lock` / `trylock` / `unlock`
- **ไม่รองรับ** condition variable · `pthread_mutex_timedlock` · rwlock · semaphore ·
  recursive mutex (ล็อกซ้ำจะนับเป็นครั้งเดียว) · process-shared mutex · static binary ·
  `fork()` แบบไม่ `exec` (TID ที่ cache ไว้จะผิด)
- **ไม่ตรวจ livelock / starvation** — thread ที่วน `trylock` แล้วปล่อยคืนยังวิ่งอยู่ ไม่มีใครค้างรอ
  จึงไม่มี wait edge และไม่มี cycle ให้เจอ (`trylock` ไม่ลง waits และไม่จดลำดับการล็อก)
  การบอกว่า "งานไม่คืบหน้า" ต้องรู้ความหมายของโปรแกรม ซึ่ง library ทั่วไปไม่รู้
- **โปรแกรมที่ใช้ condition variable** — `pthread_cond_wait` ปล่อย/ล็อก mutex คืนภายใน glibc
  โดยไม่ผ่านเครื่องมือ ตารางจึงจดเจ้าของ mutex ผิดคนเป็นช่วง ๆ ผลคือ:
  เตือน `ปลด mutex ที่ตารางจดว่า ... ถืออยู่` **ครั้งเดียว** (ที่เหลือดูได้ด้วย `DD_VERBOSE=1`) และ
  **อาจตรวจไม่เจอ** deadlock ที่ผ่าน mutex ตัวนั้น — ไม่ทำให้รายงาน deadlock ปลอม
  เพราะ thread ที่อยู่ใน `cond_wait` ไม่มี wait edge จึงอยู่ใน cycle ไม่ได้
  ([`phase5_condvar.txt`](tests/logs/phase5_condvar.txt))
- **timeline (`DD_TRACE`) เก็บเหตุการณ์ lock ได้ 5,000 ตัวแรก** โปรแกรมที่ล็อกถี่กว่านั้นจะเห็นแค่ช่วงต้น
  (รอบตรวจของ detector ยังจดครบทุกรอบ) · `DD_TRACE` ไม่ตกทอดไปยัง process ลูก (library `unsetenv` ทิ้งตอนโหลด)
  · ปิดอยู่ไม่มี overhead เพิ่ม ([`phase6_bench_ab.txt`](tests/logs/phase6_bench_ab.txt))
- **รายงานได้สูงสุด 16 cycle ต่อการตรวจหนึ่งรอบ** (`DD_MAX_CYCLES`) วงที่เกินจะไม่ถูกรายงานในรอบนั้น
  หน้า playground แสดงรายงานแค่วงสุดท้ายในภาพ (stderr ดิบยังมีครบทุกวง)
- **ตารางมีขนาดจำกัด** (รอได้ 256 thread · ถือได้ 1024 mutex · รายงาน cycle ได้ยาวสุด 64)
  เต็มแล้วจะเตือนครั้งเดียวและ **อาจตรวจไม่เจอ** deadlock ที่เกี่ยวกับรายการที่บันทึกไม่ได้
  cycle ที่ยาวเกิน 64 แสดงเป็น "(partial path, truncated)"
- **บรรทัดในโค้ด** ต้องมี `-g`, `addr2line` และโปรแกรมต้องเป็น PIE —
  binary แบบ non-PIE (`-no-pie`) ยังแสดงเป็น `at pc 0x...` (ทดสอบแล้ว)
  การหาบรรทัดเรียก `addr2line` ทีละบรรทัด รายงาน cycle ยาว ๆ จึงใช้เวลาหลายวินาที
- **poll ทุก 500 ms** ไม่ใช่ latency guarantee — รายงานออกมา "ภายหลัง" deadlock เกิด โดยทั่วไปไม่เกินหนึ่งคาบ
  แต่ไม่รับประกัน
- **ตรวจ deadlock แบบ detection** (เจอหลังค้างแล้ว) ไม่ได้ป้องกัน — ส่วนที่ "เตือนก่อนค้าง" มีแค่ lock-order inversion
- **lock-order inversion เฉพาะคู่ 2 mutex** (ถือ A ขอ B สวนกับ ถือ B ขอ A) — ลำดับที่วนผ่าน 3 ตัวขึ้นไป
  (เช่น `cycle3`: a→b→c→a) ไม่เตือนล่วงหน้า แต่ถ้าค้างจริงยังได้ `[DEADLOCK DETECTED]` ·
  ไม่นับ `trylock` (ไม่มีวันค้าง) · เตือนตามรูปแบบที่เห็น ไม่ได้พิสูจน์ว่าจะค้างจริง ·
  โปรแกรมที่ใช้ condition variable อาจได้คำเตือนผิด เพราะตารางจดเจ้าของ mutex ผิดคนเป็นช่วง ๆ ·
  จำคู่ได้ 3,072 คู่ (3/4 ของ `DD_MAX_ORDER_PAIRS`) เต็มแล้วเตือนครั้งเดียวและหยุดจดคู่ใหม่
- **path ของ `libdetect.so` ห้ามมีช่องว่างหรือ `:`** — ld.so แยก `LD_PRELOAD` ตรงอักขระพวกนี้
  library จะโหลดไม่ขึ้นแล้ว "ไม่มีรายงาน" แบบเงียบ ๆ
  `run_demo.sh` จึงใช้ path แบบ relative และ `playground.py` แจ้ง error แทนการรัน
  ถ้าโฟลเดอร์มีช่องว่าง ใช้ symlink เช่น `ln -s "<path เต็ม>" ~/dd && cd ~/dd`
- **overhead** ทุกการล็อกผ่านตารางที่มี lock ตัวเดียว จึงอาจเปลี่ยนจังหวะของโปรแกรมที่มี race
  (ตัวเลขจริงดู `tests/logs/bench.txt`)
- **`LD_PRELOAD` ตกทอดไปยัง process ลูก** รวมถึง `sh`/`addr2line` ที่ library เรียกเองตอนรายงาน
  จึงอาจได้รายงานจากโปรแกรมที่ไม่ได้ตั้งใจตรวจ (เป็นข้อดีถ้าอยากตามทั้ง process tree)

## เนื้อหาในบทเรียนที่ครอบคลุม

เงื่อนไข Coffman · wait-for graph · resource allocation graph · deadlock detection ·
deadlock prevention (lock ordering) · deadlock avoidance (Banker's Algorithm) · safe / unsafe state
