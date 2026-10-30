# แผนนำเสนอ Deadlock Detector (8:30–9:00 นาที รวม Demo · ภาษาอังกฤษ)

## 1. โครงเรื่อง 3 ช่วง

```
ช่วง A  Present เนื้อหาครบ    (0:00–5:20)
ช่วง B  Demo ที่เดียวรวด       (5:20–7:10)   ไม่นับเป็นเวลา present
ช่วง C  ผลลัพธ์ + สรุป          (7:10–8:30)
```

เส้นเรื่อง: **ปัญหา → ทฤษฎี → เครื่องมือทำงานยังไง → ทดสอบยังไง → ดูของจริง → ผล → ข้อจำกัด → สรุป**

| ลำดับ | เวลาเริ่ม | # | Slide | ยาว | ก้อน |
|---|---|---|---|---|---|
| **A** | | | **Present** | | |
| 1 | 0:00 | 1 | Title + team | 0:10 | 1 |
| 2 | 0:10 | 2 | The problem: the program just stops | 0:30 | 1 |
| 3 | 0:40 | 3 | Deadlock basics: 4 Coffman conditions | 0:25 | 1 |
| 4 | 1:05 | 4 | Our goal: find deadlocks without changing the code | 0:15 | 1 |
| 5 | 1:20 | 5 | Big picture: catch → record → check → report | 0:20 | 2 |
| 6 | 1:40 | 6 | Step 1 — Catch: `LD_PRELOAD` | 0:30 | 2 |
| 7 | 2:10 | 7 | Step 2 — Record: who holds, who waits | 0:30 | 2 |
| 8 | 2:40 | 8 | Step 3 — Check: the detector thread | 0:25 | 3 |
| 9 | 3:05 | 9 | Step 3 — Check: wait-for graph, find the loop | 0:55 | 3 |
| 10 | 4:00 | 10 | Step 4 — Report: threads, locks, code lines, picture | 0:40 | 4 |
| 11 | 4:40 | 11 | How we tested | 0:40 | 5 |
| **B** | | | **Demo (ไม่นับเป็น present)** | | |
| 12 | 5:20 | 12 | **LIVE DEMO** (4 ขั้นรวด) | 1:50 | — |
| **C** | | | **หลัง Demo: ผลลัพธ์ + สรุป** | | |
| 13 | 7:10 | 13 | Results | 0:40 | 4 |
| 14 | 7:50 | 14 | Limits & next steps | 0:15 | 5 |
| 15 | 8:05 | 15 | Summary | 0:25 | 5 |
| 16 | 8:30 | 16 | Thank you / Q&A | — | — |
| | | | **รวม** | **8:30** | (+ เวลาเปลี่ยนคนพูด ~20 วินาที → ไม่เกิน 9:00) |

**ยังไม่ได้ใส่ผู้พูด** แต่แบ่งเนื้อหาไว้เป็น 5 ก้อน ก้อนละ 1:20 เท่ากัน (ไม่นับ demo) พอตกลงกันได้ค่อยเอาชื่อใส่ทีละก้อน:

| ก้อน | สไลด์ | เวลา present |
|---|---|---|
| 1 | 1–4 เปิดเรื่อง: ปัญหา, Coffman, เป้าหมาย | 1:20 |
| 2 | 5–7 ภาพรวม, LD_PRELOAD, ตาราง | 1:20 |
| 3 | 8–9 detector, wait-for graph | 1:20 |
| 4 | 10 รายงาน (ก่อน demo) + 13 ผลลัพธ์ (หลัง demo) | 1:20 |
| 5 | 11 วิธีทดสอบ (ก่อน demo) + 14–15 ข้อจำกัด + สรุป (หลัง demo) | 1:20 |

- ทุกก้อนมีส่วนพูดก่อน demo ใครได้ก้อนไหนก็ได้ present ก่อน demo ครบทุกคน
- คนที่ได้ก้อน 5 ทำ demo ต่อจากสไลด์ 11 ได้เลย ไม่ต้องเปลี่ยนคน (หรือให้คนอื่นทำ demo ก็ได้ เพราะไม่นับเวลา)

**เนื้อหาครอบคลุมอะไรบ้าง:**
- **ทฤษฎีจากบทเรียน:** deadlock, Coffman 4 ข้อ, wait-for graph, detection / prevention / avoidance
- **การออกแบบเครื่องมือครบ 4 ขั้น:** ดักจับด้วย LD_PRELOAD → จดลงตาราง → detector หาวง → พิมพ์รายงาน
- **การทดสอบ:** โปรแกรมทดสอบ, การใช้ barrier แทน sleep, regression test
- **ผลลัพธ์:** ตารางผลรัน, ตรวจเจอ 20/20 รอบ, overhead
- **ข้อจำกัดและการเตือนล่วงหน้า:** lock-order inversion (เตือนเมื่อสอง lock ถูกล็อกสลับลำดับ)
- **สรุป:** แนวคิดจากบทเรียน, Banker's simulator, หน้า playground

## 2. แผน Demo (1:50 นาที)

`tests/run_demo.sh` พิมพ์คำอธิบายเป็นภาษาไทย ถ้านำเสนอเป็นอังกฤษ ให้พิมพ์คำสั่งเองใน terminal (WSL) แล้วพูดอธิบายเป็นอังกฤษ:

```bash
# ครั้งแรกครั้งเดียว: path จริงมีช่องว่าง ("Bachelor degree") ถ้าใช้ path เต็ม LD_PRELOAD จะโหลดไม่ขึ้นแบบเงียบ ๆ
# ln -s "/mnt/c/Users/frank/Documents/Bachelor degree/3. Junior/Os/ProjectOs/deadlock-detector" ~/dd
cd ~/dd
make

timeout 3 ./build/bank                                        # 1) hangs, nothing printed (exit 124)
timeout 3 env LD_PRELOAD=./build/libdetect.so ./build/bank    # 2) same command + LD_PRELOAD → [DEADLOCK DETECTED] + bank.c:65/70/89/94
DD_DOT_OUT=cycle.dot timeout 3 env LD_PRELOAD=./build/libdetect.so ./build/bank; dot -Tpng cycle.dot -o cycle.png   # 3) picture of the loop
timeout 15 env LD_PRELOAD=./build/libdetect.so ./build/no_deadlock   # 4) same lock order → exits 0, no report
```

ถ้าเวลาเกิน ให้ตัดขั้น 3 ออกก่อน เพราะสไลด์ 10 มีภาพแทนอยู่แล้ว

**กันพัง:**
- รัน `make` และลองทุกคำสั่งก่อนขึ้นพูด 1 รอบ
- เปิด terminal ไว้ก่อนเริ่ม พิมพ์คำสั่งเตรียมไว้ใน history (กดลูกศรขึ้นเรียกได้เลย ไม่ต้องพิมพ์สด)
- ขยายฟอนต์ terminal ให้ใหญ่ (≥ 20pt)
- อัดวิดีโอ demo สำรองไว้ และใส่ภาพ output จริงไว้ในสไลด์ที่ 10 ถ้า live พังให้ใช้ภาพแทนได้ทันที
- ซ้อมจับเวลาจริงทั้งชุดอย่างน้อย 2 รอบ

## 3. ข้อความที่ต้องเช็คก่อนส่งให้ AI

- **ชื่อภาษาอังกฤษ** (ใช้บนสไลด์ Title และ Thank you): ผมถอดเสียงให้ก่อน ให้แก้ตามตัวสะกดที่ใช้ในระบบทะเบียนจริงของแต่ละคน
- **ชื่อวิชา / มหาวิทยาลัย / อาจารย์**: README มีแค่รหัสวิชา `03603332 Operating Systems` เติมส่วนที่เหลือเอง

---

## 4. Prompt (คัดลอกทั้งก้อนไปใช้)

ใช้กับ Claude Code ในโฟลเดอร์นี้ได้ทันที (จะใช้ skill `frontend-slides`) หรือใช้กับ AI ตัวอื่นก็ได้

````text
Use the frontend-slides skill to build a 16-slide HTML presentation (16:9) in English for a
university talk of 8:30 to 9:00 minutes. Then export it to PDF.
Save to presentation/deadlock-detector-slides.html and presentation/deadlock-detector-slides.pdf.

STRUCTURE (3 parts)
A. Presentation (slides 1–11): the full content.
B. Live demo (slide 12): one block, no interruptions.
C. After the demo (slides 13–16): results, limits, summary, Q&A.
The content must be complete: the theory from class, all 4 parts of the tool, how we tested it,
the results, the limits, and a summary.

SPEAKERS: NOT DECIDED YET
Do not assign speakers. Do not put speaker names or numbers on slides, in footers or in notes.
Do not write handover lines with names. Write speaker notes per slide only.

CONTEXT
Project: "Deadlock Detector" — a Linux LD_PRELOAD library (libdetect.so, written in C) that
finds mutex deadlocks in multithreaded programs without changing their code, and shows the
exact source lines.
Course: 03603332 Operating Systems, Group Project, chapter "Deadlocks".
Source of truth: deadlock-detector/README.md, deadlock-detector/docs/design.md and the test
programs in deadlock-detector/tests/.
Use only numbers and output that appear in those files or in deadlock-detector/tests/logs/.
Do not invent figures.

TEAM (names + student IDs, for the title slide and the thank-you slide only)
Mr. Supawit Chaiyanusak      6730300591
Mr. Chayanin Iamsakul        6730300094
Mr. Pattanadech Mahimuang    6730300850
Mr. Thatthorn Jiamton        6730300779
Mr. Phutawan Supkerd         6730300884

AUDIENCE & TONE
Classmates and an OS lecturer. Presenters are Thai students, not native English speakers.
One idea per slide, big diagrams, very little text (max ~20 words of body text per slide).
Every technical slide should have a diagram or code/terminal block, not a wall of bullets.

PLAIN ENGLISH RULES (apply to slide text AND speaker notes)
- Use everyday words (about CEFR B1 level). Say "use" not "utilize", "stop" not "terminate",
  "check" not "verify", "slow" not "incur overhead", "put in front of" not "interpose".
- SHORT sentences: max 10 words each, one idea each. No long clauses, no passive voice,
  no idioms. Two short sentences are better than one long one.
- Keep only the technical words the course needs: deadlock, thread, mutex (lock), cycle,
  wait-for graph, LD_PRELOAD, Coffman conditions. The first time each appears, explain it in a
  few plain words, e.g. "A mutex is a lock. Only one thread can hold it."
- Simpler words must not change the meaning. Keep every number, name and claim exactly as in
  the source files. If a plain word would be wrong, keep the technical word and explain it.
- Speaker notes must be easy to say out loud: avoid words that are hard for Thai speakers to
  pronounce when a simpler word exists.

TIMING
Total speaking time 8:30 (6:40 presenting + 1:50 demo). Speaker changes take ~20 s,
so the talk ends by 9:00.
Write speaker notes at about 110 words per minute (a calm, non-native pace).
Word budget per slide = target time × 110 / 60. Do not go over it.
Put the target time and word count at the top of each slide's notes.

SLIDES (target time per slide)
The text below says what each slide must cover. Write the final wording with the rules above.

--- PART A: PRESENTATION ---
1. Title — "Deadlock Detector: Find Deadlocks Without Changing the Code", course,
   team names + IDs. 0:10
2. The Problem — two threads move money. One goes A to B. One goes B to A.
   Each one locks one account, then waits for the other. Show the 2-column lock timeline.
   Key line: "No crash. No error. 0% CPU. It just stops." 0:30
3. Deadlock Basics — the 4 Coffman conditions: mutual exclusion, hold and wait,
   no preemption, circular wait. All 4 must be true for a deadlock.
   Map each one to the bank example with one short line. 0:25
4. Our Goal — find the deadlock. Show which threads, which locks and which code lines.
   No code change. No rebuild. Show the one-line usage:
   LD_PRELOAD=./build/libdetect.so ./your_program 0:15
5. Big Picture — one diagram of the 4 steps, used as a map for the next slides:
   1. Catch every lock call  2. Record who holds and who waits
   3. Check for a loop every 500 ms  4. Report it.
   Show the source files under each step (interpose.c, state.c, detector.c + graph.c, report.c). 0:20
6. Step 1 — Catch: LD_PRELOAD — LD_PRELOAD loads our library first. So our lock function runs
   before the real one. Layer diagram: program → libdetect.so (1. note "waiting",
   2. call the real lock, 3. note "got it") → libc.so.6 → Linux kernel (we do not touch it).
   We catch 3 functions: pthread_mutex_lock, trylock, unlock. 0:30
7. Step 2 — Record: two tables. holds[]: which thread holds each lock. waits[]: which lock each
   thread waits for. The tables have a fixed size. Why: we cannot use malloc here. malloc uses
   locks inside, so it would call our code again in a loop. A per-thread flag (in_dd) also
   stops this loop. 0:30
8. Step 3 — Check: the detector thread. Our own thread wakes up every 500 ms. It copies the
   tables. It never uses the program's locks. So it keeps running when all program threads
   are stuck. 0:25
9. Step 3 — Check: wait-for graph. Each thread is a circle. An arrow T1 → T2 means "T1 waits
   for a lock T2 holds". Step through the t0–t3 table from the README: T1 locks A. T2 locks B.
   T1 waits for B (arrow T1 → T2). T2 waits for A (arrow T2 → T1). The arrows make a loop.
   A loop = circular wait = deadlock. A thread waits for only one lock at a time, so each
   circle has at most one arrow out. To find a loop, we just follow the arrows.
   It finds every loop in one check (up to 16), and reports each loop only once. 0:55
10. Step 4 — Report: show the real output block from README ([DEADLOCK DETECTED] 2 threads in
    cycle … bank.c:65/70/89/94 … Cycle: T514 -> T513 -> T514, Hint: acquire locks in a
    consistent global order). Explain each line: "holds" = it has this lock, got it at this
    line; "waiting" = it is stuck at this line; "Cycle" = the loop; "Hint" = how to fix it.
    Code lines come from dladdr + addr2line. The program must be built with -g and as PIE.
    DD_DOT_OUT also saves the loop as a picture (Graphviz).
    This slide is also the backup if the live demo fails. 0:40
11. How We Tested — test programs: bank, sample_webapp, cycle3 (3-thread loop), two_pairs
    (2 loops at once), no_deadlock (must give no report). Edge cases: robust, errcheck,
    longcycle (70 threads), condvar, lockorder (opposite order, no deadlock → must only warn).
    We use pthread_barrier, not sleep, so the deadlock
    happens every time. make regress: every edge-case test fails if we remove its fix.
    End with: "Now let us show you the real thing." 0:40

--- PART B: LIVE DEMO ---
12. LIVE DEMO — one block. A mostly empty slide with 4 small captions:
    (1) ./build/bank stops forever (exit 124)
    (2) same command + LD_PRELOAD → report with code lines
    (3) DD_DOT_OUT → picture of the loop
    (4) no_deadlock: all threads lock in the same order → it finishes (exit 0), no report.
    Speaker notes: one short line per step, what to type and what to say.
    Note in the notes: if time runs short, skip step 3. 1:50

--- PART C: AFTER THE DEMO (results + summary) ---
13. Results — test table from README (bank, no_deadlock, sample_webapp, cycle3, two_pairs,
    lockorder: without vs with LD_PRELOAD; lockorder exits 0 with one order warning). It found the deadlock in 20 of 20 runs for bank and
    sample_webapp. Cost per lock + unlock: 1 thread 2.3 → 72.1 ns (×31.3);
    4 threads, 1 shared lock 39.9 → 313.5 ns (×7.9).
    Takeaway: good for finding bugs. Too slow to leave on all the time. 0:40
14. Limits & Early Warning — only normal mutex locks (no condition variables, rwlock, semaphore).
    It finds a deadlock after it happens. It does not stop it.
    Also: it warns early when two locks are taken in opposite orders (lock-order inversion). 0:15
15. Summary — 3 key points of the whole talk:
    (1) a deadlock is a loop in the wait-for graph,
    (2) our tool finds it with no code change and shows the exact lines,
    (3) the fix is prevention: lock in the same order (demo step 4).
    Also link to class: detection (this tool), prevention (lock order + our early warning),
    avoidance (our Banker's Algorithm simulator, docs/banker.html).
    End with one strong closing line. 0:25
16. Thank You / Q&A — team names again.

DESIGN
- Dark technical theme suits a systems topic (terminal-style code blocks, monospace for code,
  one accent colour for "waiting"/deadlock arrows, e.g. red, and one for "holding", e.g. green).
- Use the same red/green colours in the diagrams on slides 2, 6, 9 and 10.
- Reuse the 4-step diagram from slide 5 as a small progress bar on slides 6–10,
  highlighting the current step.
- Show the part (A / B / C) as a small label and the slide number in the footer.
- Keyboard navigation (← →) for presenting. No auto-advance.

SPEAKER NOTES
Add speaker notes for every slide, following the plain English rules and the word budgets.
Also export them as presentation/speaker-notes.md, one section per slide, in slide order,
with the target time of each slide.

PDF EXPORT
Print CSS so each slide becomes exactly one 16:9 landscape PDF page, all animations shown in
their final state, backgrounds included. Check that the PDF has 16 pages and no slide overflows.
````
