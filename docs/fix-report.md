# รายงานการแก้เพื่อเพิ่ม Perfection

เป้าหมาย: ทำให้เครื่องมือ "ทำงานจริง พิสูจน์ได้ และไม่อ้างเกินจริง" โดยไม่เพิ่ม feature นอกรายการ
ทำตามลำดับ Phase 0 → 3 และ 4.1 · 1 ข้อ = 1 commit · หลักฐานทุกข้ออยู่ใน [`tests/logs/`](../tests/logs/)

## สภาพแวดล้อมและวิธีตรวจ

| รายการ | ค่า |
| --- | --- |
| OS | Ubuntu 26.04 บน WSL2 (kernel 6.6.114.1-microsoft-standard-WSL2) |
| glibc / gcc | 2.43 / 15.2.0 |
| CPU | 13th Gen Intel Core i9-13900H, 20 cores |
| ที่รัน | `~/dd` = symlink ไปยังโฟลเดอร์โปรเจกต์ (path จริงมีช่องว่าง — ดูข้อค้นพบ A) |

**ทุกข้อตรวจด้วยวิธีเดียวกัน:** `make clean && make` ต้องไม่มี warning แล้วรัน test matrix 6 กรณี
(bank / no_deadlock / sample_webapp × มี/ไม่มี `LD_PRELOAD`) โดยเก็บ stdout, stderr และ exit code แยกไฟล์

| กรณี | เกณฑ์ผ่าน |
| --- | --- |
| bank, sample_webapp ไม่มี preload | exit 124 (ค้างจน `timeout` ตัด) ไม่มี marker |
| bank, sample_webapp มี preload | exit 124 และ stderr มี `[DEADLOCK DETECTED]` |
| no_deadlock ทั้งสองแบบ | exit 0 ไม่มี marker |

ทุกข้อผ่าน 6/6 ก่อนไปข้อถัดไป หัวไฟล์ log แต่ละไฟล์มีคำสั่งที่ใช้รันไว้ ใช้รันซ้ำเองได้
(สคริปต์ที่วนรัน matrix อยู่นอก repo ไม่ได้ commit เข้าไป)

---

## Phase 0 — Baseline

**เจออะไร:** ตอนรันจากโฟลเดอร์จริง `.../Bachelor degree/3. Junior/...` กรณี preload ทั้งสองตัว **ไม่มีรายงานเลย**
stderr บอกว่า `ld.so: object '/mnt/c/Users/frank/Documents/Bachelor' ... cannot be preloaded`
แปลว่า ld.so แยกค่า `LD_PRELOAD` ตรงช่องว่าง library จึงไม่ถูกโหลด
โปรแกรมยังค้างตามปกติ ถ้าไม่ดู stderr ดิบจะดูเหมือน "เครื่องมือหา deadlock ไม่เจอ"

**ทำอะไร:** เก็บผลชุดนี้ไว้เป็นหลักฐานใน `baseline_pathspace/` แล้วรันใหม่จาก `~/dd` ที่ไม่มีช่องว่าง
เพิ่ม `sample_webapp` เข้า Makefile ตามที่ spec สั่ง

**ผล:** matrix ผ่าน 6/6 · bank แบบ preload มีรายงาน **10/10 รอบ**
(`env.txt`, `baseline_*.txt`, `baseline_bank_preload_x10.txt`) · commit `5601048`

## Phase 1

| ข้อ | เจออะไร / ทำไมต้องแก้ | ทำอะไร | ตรวจยังไง ผลเป็นอย่างไร | commit |
| --- | --- | --- | --- | --- |
| 1.1 | `usleep` ระหว่างสอง lock แค่ "น่าจะ" นานพอ ไม่ได้รับประกันว่าอีก thread ได้ lock ตัวแรกไปแล้ว | ใช้ `pthread_barrier` (count 2) ใน `bank.c` และ `sample_webapp.c`: lock ตัวแรก → barrier → lock ตัวที่สอง · เช็ค return ของ init/wait/destroy (`SERIAL_THREAD` ไม่ใช่ error) · ไม่ใส่ใน `no_deadlock.c` · แก้ README | รัน preload 20 รอบ: **bank 20/20, sample_webapp 20/20** · เปิด `DD_VERBOSE` ดูตาราง เห็นแค่ 2 mutex / 2 tid barrier ไม่ทำให้มี entry แปลก | `ad9a53d` |
| 1.2 | `static volatile int g_stop` เป็น data race (volatile ไม่ใช่ synchronization ใน C11) | เปลี่ยนเป็น `atomic_int` + `atomic_load` / `atomic_store` | matrix 6/6 | `0928ce5` |
| 1.3 | — | เก็บ matrix หลัง Phase 1 | `phase1_*.txt` ผ่าน 6/6 | `02f3658` |

## Phase 2

| ข้อ | เจออะไร / ทำไมต้องแก้ | ทำอะไร | ตรวจยังไง ผลเป็นอย่างไร | commit |
| --- | --- | --- | --- | --- |
| 2.1 | `run_demo.sh` นับผ่าน/ไม่ผ่านแค่ขั้น 5 ถ้า detector ไม่ทำงานก็ยังพิมพ์ "ผ่านทุกขั้น" · ใช้ path เต็มกับ `LD_PRELOAD` จึงพังในโฟลเดอร์ที่มีช่องว่าง | `make` ทุกครั้ง · ขั้น 1–2 ต้อง exit 124 · ขั้น 2 ต้องมี marker · ขั้น 3 `.dot` ต้องมีและไม่ว่าง · ขั้น 4 ต้อง exit 0 และไม่มี marker · `fail` สะสม · ขั้น 5 ย้ายไป `--smoke` · เพิ่ม `--no-pause` · ใช้ path แบบ relative | รันจากโฟลเดอร์ที่มีช่องว่าง → ผ่านทุกขั้น exit 0 · `--smoke` ผ่าน · **ทดสอบกรณีไม่ผ่าน:** ชี้ไปที่ .so ที่ไม่มีอยู่ → ขั้น 2–3 ไม่ผ่าน exit 1 ตามคาด (`phase2_run_demo_*.txt`) | `57267f6` |
| 2.2 | lock/trylock/unlock คืน 0 ตอน resolve ไม่ได้ ทั้งที่ไม่ได้ล็อกจริง → mutual exclusion เสียแบบเงียบ ๆ | resolve ด้วย `dlsym` + เช็ค `dlerror()` · ถ้ายัง NULL เขียนด้วย `write(2)` แล้ว `abort()` · แก้ comment ที่บอกว่าเป็น wart ที่ยอมรับได้ | matrix 6/6 ไม่มี abort (บน glibc 2.43 dlsym/malloc ไม่ผ่าน `pthread_mutex_lock`) | `2327124` |
| 2.3 | robust mutex คืน `EOWNERDEAD` = ได้ lock แล้ว แต่โค้ดจดเฉพาะ `rc == 0` | `lock_was_acquired(rc)`: `rc == 0 \|\| rc == EOWNERDEAD` ทั้ง lock และ trylock | โปรแกรมทดสอบชั่วคราว: เจ้าของตายขณะถือ lock → main ได้ rc=130 (EOWNERDEAD) แล้วตารางบันทึก main เป็นเจ้าของ waiter ถัดมาเห็น edge ไปหา main | `1d1bb8f` |
| 2.4 | `dd_state_released` ลบ entry แม้คนที่ unlock ไม่ใช่เจ้าของ (ERRORCHECK จะคืน EPERM เจ้าของจริงยังถืออยู่) | ลบเฉพาะเมื่อ `holder == t` ไม่ตรงให้คง entry ไว้และพิมพ์ warning | โปรแกรมทดสอบชั่วคราว: unlock จากคนอื่นได้ rc=1 (EPERM) มี warning และ waiter ยังเห็น edge ไปหาเจ้าของจริง | `e26396d` |
| 2.5 | เทียบ `<` ระหว่าง pointer ของ global คนละ object เพื่อเรียงลำดับ = UB ใน ISO C · โปรแกรมจบใน ~0.2 s เร็วกว่าคาบ detector ไม่ได้พิสูจน์ว่า "ตรวจแล้วไม่เตือนผิด" | เพิ่ม field `id` แล้วเรียงตาม id (แก้ตัวอย่าง ordered ใน `playground.html` ด้วย) · วนโอน 10 รอบต่อ thread และเช็คเวลาจริง ≥ 1500 ms (3 คาบ) | ใช้เวลา **2004 ms**, detector ตรวจ **3 รอบ**, ไม่มีรายงาน (`phase2_no_deadlock_checks.txt`) · compile ตัวอย่างจาก html แล้วรันได้ exit 0 | `b8a5fb2` |
| 2.6 | แก้ header แล้ว `make` ไม่ build ใหม่ | `-MMD -MP` + `-include build/*.d` · target `sample_webapp`, `bench` · `demo` เรียกผ่าน `bash` | แตะ `src/graph.h` แล้ว build ใหม่เฉพาะ detector.c, graph.c, report.c ที่ include มัน | `a77d063` |
| 2.7 | cycle ยาวเกิน `DD_MAX_CYCLE_LEN` (64) ถูกตัดแล้วรายงาน/DOT ปิดวงกลับ T0 = วาดวงที่ไม่มีจริง | เพิ่ม `partial` + `full_len` · หัวรายงานและบรรทัด Cycle บอก "(partial path, truncated)" ไม่ปิดวง · DOT ชี้ไป node `...` แบบเส้นประ | สร้างวง 70 thread ชั่วคราวแล้วรัน (`phase2_partial_cycle.txt`) — **เจอบั๊กเพิ่ม 2 ตัวระหว่างทดสอบ** แก้ใน commit เดียวกัน: (1) warning "cycle ยาวเกิน" พิมพ์ซ้ำทุก 500 ms → เปลี่ยนเป็น debug log (2) บรรทัด Cycle ยาวเกิน buffer 512 ของ `dd_logf` ถูกตัดท้าย ป้าย partial หายไป → พิมพ์ทีละช่วง | `fd7ffe8` |
| 2.8 | `playground.html` hard-code `/mnt/d/Project_OS/...` · ใช้คำว่า "โหลดได้" ทั้งที่แค่เช็คว่ามีไฟล์ · exit ผิดปกติ (เช่นโดน signal) ขึ้นป้าย "จบเอง" เหมือนสำเร็จ · path มีช่องว่างแล้วรันได้แต่ไม่มีรายงาน | เอา path ที่ hard-code ออก · "พบไฟล์" · `outcome` = ok / timeout / abnormal หน้าเว็บแสดง "จบแบบผิดปกติ" · `lib_path_problem()` ตอบ error ชัดเจนพร้อมวิธีแก้ · หา ROOT จาก `$PWD` + argv เพราะ `resolve()` แปลง symlink กลับเป็น path ที่มีช่องว่าง | เรียก API จริง (`phase2_playground_api.txt`): โฟลเดอร์จริง → error ชัดเจน · `~/dd` → ค้าง + มีรายงาน · โค้ดที่ `abort()` → abnormal, exit -6 · ตรวจ syntax ของ JS ด้วย node แล้ว **แต่ยังไม่ได้ลองกดปุ่มบนหน้าเว็บจริง** | `4ffe8e3` |
| 2.9 | เอกสารอ้างเกินโค้ด: "ดัก pthread_mutex_*", "ขอแค่ libpthread", "ใช้กับของจริงได้ทันที", "มี cycle = deadlock", "implement ครบ", output ตัวอย่างที่เขียนเอง, "non-PIE dli_fbase = 0", "popen = fork + exec" | แก้ README, `design.md`, `overview.html` และ comment ในโค้ดตามรายการ · ตาราง scope จริง · หัวข้อ Limitations ครบทุกข้อ + path ที่มีช่องว่าง · ใช้ output และเวอร์ชันจริงจาก log | **ทดสอบก่อนเขียน:** build `bank` แบบ `-no-pie` → รายงานเป็น `at pc 0x401486` จริง · build แบบ `-static` → ไม่มีรายงาน · เปิด `overview.html` ในเบราว์เซอร์ดู layout | `80891dc` |

## Phase 3

| ข้อ | ทำอะไร | ผล | commit |
| --- | --- | --- | --- |
| 3.1 | `tests/bench.c` + `tests/run_bench.sh` (`make bench`): NORMAL mutex 1,000,000 คู่ · `CLOCK_MONOTONIC` · วอร์มอัพ 1 รอบ วัด 15 รอบ รายงาน median · uncontended / contended (4 thread เริ่มพร้อมกันด้วย barrier) | ตารางด้านล่าง (`bench.txt`) | `c589982` |
| 3.2 | DFS เก็บทุกวงแทนการหยุดที่วงแรก (out-degree ≤ 1 ทำให้แต่ละวงไม่มี node ร่วมกัน) · dedup ต่อวง · DOT รวมทุกวงเป็น cluster · `tests/two_pairs.c` | รายงาน 2 ครั้งพอดี **10/10 รอบ** · `dot` parse ไฟล์ได้ · DOT ของวงเดียวยังเหมือนเดิม · ตรวจ partial cycle ซ้ำแล้วยังถูก (เจอ warning `-Wformat-truncation` ของ buffer ชื่อ node แก้แล้ว) | `1dd69bb` |
| 3.3 | `tests/cycle3.c`: a→b→c→a ใช้ barrier(3) | `3 threads in cycle` **20/20 รอบ** ชี้บรรทัดครบ 6 จุด | `663cbb8` |

**Overhead** (median ของ 15 รอบ, ns ต่อคู่ lock+unlock)

| กรณี | ไม่มี LD_PRELOAD | มี LD_PRELOAD | เพิ่มขึ้น |
| --- | --- | --- | --- |
| uncontended (1 thread) | 2.3 | 72.8 | +70.5 (×31.7) |
| contended (4 thread, 1 mutex) | 38.7 | 296.1 | +257.4 (×7.7) |

## Phase 4.1 — ตรวจ zip จากของสะอาด

`git archive --format=zip HEAD` (`663cbb8`) → แตกในโฟลเดอร์ใหม่ → `make` ไม่มี warning →
`bash tests/run_demo.sh --no-pause` **ผ่านทุกขั้น exit 0** (`phase4_clean_zip.txt`) · commit `88853d1`

## Phase 5 — หลังรีวิวรอบสอง (หลัง merge PR #1)

ทดสอบบน**อีกเครื่อง**: glibc 2.39 · gcc 13.3.0 · x86-64 (คนละรุ่นกับ env.txt) — `make` ไม่มี warning,
`run_demo.sh --no-pause` และ `--smoke` ผ่านทุกขั้น, bank preload 10/10, เลขบรรทัด 65/70/89/94 ตรง

| ข้อ | เจออะไร / ทำไมต้องแก้ | ทำอะไร | ตรวจยังไง ผลเป็นอย่างไร | commit |
| --- | --- | --- | --- | --- |
| 5.1 | โปรแกรม condvar ธรรมดา (ping-pong 2 thread ไม่มี deadlock) ได้ **WARNING ~3,500 บรรทัด** — `pthread_cond_wait` ปล่อย/ล็อก mutex คืนภายใน glibc ไม่ผ่าน wrapper ตารางจึงจดเจ้าของผิดคน แล้วทุก unlock ตกกรณี `holder != t` ของข้อ 2.4 | เตือนครั้งแรกพร้อมบอกสาเหตุ ครั้งถัดไปเป็น `dd_dbgf` · พฤติกรรมตารางเหมือนเดิม (ไม่เพิ่ม feature condvar) · `tests/condvar.c` · Limitations ใน README / overview.html | ก่อนแก้ 3567 บรรทัด → หลังแก้ 1 บรรทัด 5/5 รอบ ไม่มีรายงาน deadlock (`phase5_condvar.txt`) · ไล่โค้ดแล้ว: thread ใน `cond_wait` ไม่มี wait edge จึงไม่ทำให้เกิด cycle ปลอม ผลเสียคืออาจตรวจไม่เจอ | `290903a` |
| 5.2 | โปรแกรมทดสอบของ 2.3 / 2.4 / 2.7 อยู่นอก repo — แก้โค้ดภายหลังแล้วไม่มีอะไรจับได้ | `robust.c` · `errcheck.c` · `longcycle.c` · `run_regress.sh` รวม condvar ด้วย · `make regress` · ทุกตัวใช้ barrier | ผ่านทุกข้อ · **ถอย fix ทีละข้อชั่วคราวแล้วข้อนั้นไม่ผ่านทุกครั้ง** = เทสจับบั๊กได้จริง (`phase5_regress.txt`) | `75b8e9d` |
| 5.3 | ตัวอย่าง bank / cycle3 ใน playground ยังใช้ `usleep` แต่ comment บอก "ค้างทุกครั้ง" · ตารางข้อจำกัดในหน้าเว็บยังใช้คำที่ 2.9 ระบุว่าอ้างเกินจริง · ปุ่มยังไม่เคยกดจริง | bank / cycle3 ใช้ barrier · ordered คง `usleep` พร้อมเหตุผล · ตารางข้อจำกัดตรงกับ README | compile ด้วย flag ของ playground.py ไม่มี warning · bank 20/20, cycle3 20/20 (`phase5_playground_presets.txt`) · **กดปุ่มจริง**ด้วย headless Chromium 5 ตัวอย่าง × 2 ปุ่ม ถูก 10/10 ไม่มี JS error (`phase5_playground_ui.txt`) | `f6db42c` |

## Phase 6 — timeline ทีละขั้นในหน้า playground

ทดสอบบน WSL2 kernel 6.18.33.2 · x86-64 — `make` ไม่มี warning, `run_demo.sh --no-pause` ผ่านทุกขั้น
(`phase6_run_demo.txt`), `make regress` ผ่านทุกข้อรวมข้อใหม่ (`phase6_regress.txt`)

| ข้อ | เจออะไร / ทำไมต้องแก้ | ทำอะไร | ตรวจยังไง ผลเป็นอย่างไร |
| --- | --- | --- | --- |
| 6.1 | หน้า playground โชว์แค่ผลสุดท้าย (รายงาน + ภาพวง) ไม่เห็นว่าเครื่องมือตรวจเจอ**อย่างไร** | `src/trace.c/h`: env `DD_TRACE` จดเหตุการณ์ wait / unwait / acq / rel / relx / check / cycle ลงไฟล์ ใต้ `dd_state_lock` · `playground.py` อ่านไฟล์ แปลง pc เป็นบรรทัดด้วย `addr2line` ครั้งเดียว · หน้าเว็บเล่นย้อนทีละขั้น: ภาพกราฟ ตาราง holds / waits ไฮไลต์บรรทัด ปุ่มเล่น / ไปตอนเจอวง | headless Edge 5 ตัวอย่าง ไม่มี JS error ที่ 1100 px และ 390 px โหมดสว่าง / มืด · bank ได้บรรทัด 65 / 89 / 94 / 70 ตรงกับรายงาน (`phase6_playground_ui.txt`) |
| 6.2 | `addr2line` ที่ report.c เรียกได้ `LD_PRELOAD` + `DD_TRACE` ไปด้วย แล้วเปิดไฟล์ trace ด้วย `O_TRUNC` ลบ timeline ทิ้ง | `unsetenv("DD_TRACE")` ใน constructor · ข้อ trace ใน `run_regress.sh` | ถอย `unsetenv` ออกชั่วคราว → ข้อ "wait ครบ 4 ครั้ง" ได้ 0 ไม่ผ่าน · ใส่คืน → ผ่าน |
| 6.3 | รอบแรกช้าลงราว 10% **ทั้งที่ไม่ได้เปิด** trace (function call ข้ามไฟล์ 4 ครั้งต่อคู่ lock+unlock) | เช็ค `DD_TRACING()` ที่จุดเรียกใน `state.c` (`extern int` แบบ hidden) | A/B กับ library ก่อน Phase 6 รันสลับกัน: uncontended 97–99 ns ทั้งคู่ · contended 364–374 ns ทั้งคู่ (`phase6_bench_ab.txt`) |
| 6.4 | อีก thread แทรกกลาง wait → unwait → acq ทำให้ "ล็อกได้ทันที" แสดงเป็น "ต้องรอ" · ลูกศรสองทิศทับกัน | รวมขั้นจากเหตุการณ์ถัดไปของ thread เดียวกัน · แยกความสูง / ฝั่งของเส้นโค้ง | ภาพหลังแก้ใน `phase6_playground_ui.txt` |

## Test matrix ก่อน / หลัง

| กรณี | baseline (path มีช่องว่าง) | baseline (`~/dd`) | หลัง Phase 1 | หลัง Phase 2 |
| --- | --- | --- | --- | --- |
| bank ไม่มี preload | ✅ 124 | ✅ 124 | ✅ 124 | ✅ 124 |
| bank มี preload | ❌ 124 ไม่มีรายงาน | ✅ 124 + รายงาน | ✅ | ✅ |
| no_deadlock ไม่มี preload | ✅ 0 | ✅ 0 | ✅ 0 | ✅ 0 (2.0 s) |
| no_deadlock มี preload | ✅ 0 | ✅ 0 | ✅ 0 | ✅ 0 ไม่มีรายงาน |
| sample_webapp ไม่มี preload | ✅ 124 | ✅ 124 | ✅ 124 | ✅ 124 |
| sample_webapp มี preload | ❌ 124 ไม่มีรายงาน | ✅ 124 + รายงาน | ✅ | ✅ |

---

## ข้อค้นพบนอกรายการใน spec

| | เจออะไร เจอยังไง | ทำอะไรไป |
| --- | --- | --- |
| A | **path ที่มีช่องว่างทำให้ library ไม่โหลดแบบเงียบ ๆ** — เจอตอน Phase 0 จาก stderr ของ ld.so | หยุดรายงานก่อน แล้วแก้ตามที่อนุมัติ: run_demo ใช้ path แบบ relative · playground แจ้ง error · เขียนใน Limitations · ทดสอบจาก `~/dd` |
| B | บรรทัด Cycle ยาวเกิน 512 byte ถูกตัด — เจอตอนทดสอบ 2.7 | แก้ใน 2.7 |
| C | warning "cycle ยาวเกิน" พิมพ์ซ้ำทุกคาบ — เจอตอนทดสอบ 2.7 | แก้ใน 2.7 |
| D | รายงาน cycle ยาว ๆ ใช้เวลาหลายวินาที เพราะเรียก `addr2line` ทีละบรรทัด — ตอนแรกตั้ง timeout ไว้ 4 s แล้วรายงานออกมาไม่ครบ | เขียนใน Limitations ไม่ได้แก้ |
| E | `LD_PRELOAD` ตกทอดไปถึง `sh`/`addr2line` ที่ library เรียกเอง — เห็น "state: พร้อม" 5 ครั้งใน verbose log ของโปรเซสเดียว | เขียนใน Limitations และ `design.md` |
| F | playground แสดงในภาพแค่วงสุดท้าย เมื่อมีหลายวง (ผลข้างเคียงของ 3.2) | เขียนใน Limitations ไม่ได้แก้ เพราะ spec ห้ามขยาย playground |
| G | non-PIE แสดงเป็น pc · static binary ไม่มีรายงานเลย — ทดสอบตอนทำ 2.9 | เขียนใน Limitations |

## ยังค้าง

- **4.2–4.4** ซ้อมและ build ใหม่บนเครื่องที่จะใช้ present จริง · ทำ PDF (ชื่อ + รหัสนิสิตทุกสไลด์, slide Limitations ต้องตรงกับ README — มีข้อ condvar เพิ่มจาก 5.1) · ซ้อมจับเวลา 2 รอบให้จบใน 9:30 นาที
- commit ของงานนี้ลงท้ายด้วย `Co-Authored-By: Claude` ถ้าวิชาไม่ต้องการบรรทัดนี้ต้องแก้ก่อนส่ง
- เลข thread id และที่อยู่ mutex ในรายงานเปลี่ยนทุกครั้งที่รัน ตัวอย่างใน README เป็นของรอบหนึ่ง
- ~~ตัวอย่าง bank / cycle3 ใน playground ใช้ `usleep`~~ แก้แล้วใน 5.3
- ~~โปรแกรมทดสอบชั่วคราวของ 2.3 / 2.4 / 2.7 ไม่อยู่ใน repo~~ เป็น `make regress` แล้วใน 5.2
- `tests/logs/bench.txt` ยังเป็นผลของเครื่องเดิม (kernel 6.6) ตัวเลขใน README มาจากไฟล์นั้น ไม่ได้วัดใหม่ใน Phase 6
  (Phase 6 วัดแค่ A/B ว่า trace ไม่เพิ่ม overhead)
- ~~ปุ่มบนหน้า playground ยังไม่ได้ลองกดจริง~~ กดจริงแล้วใน 5.3 (headless Chromium บน Linux — ยังไม่ได้ลองบนเบราว์เซอร์ของเครื่องที่จะ present)
