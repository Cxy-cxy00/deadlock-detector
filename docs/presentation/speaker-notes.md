# Speaker Notes — Deadlock Detector

Total 8:30 (6:40 presenting + 1:50 demo). Pace: about 110 words per minute.
Speakers are labelled **A–E** (names to be filled in later). In the slides, the label is in the footer, and **N** shows these notes.

| Speaker | Slides | Before the demo | After the demo | Presenting time |
|---|---|---|---|---|
| **A** | 1–4 · title, problem, Coffman, goal | 1:20 | — | **1:20** |
| **B** — นายศุภวิชญ์ ไชยานุศักดิ์ 6730300591 | 5–7 · big picture, LD_PRELOAD, tables | 1:20 | — | **1:20** |
| **C** | 8–9 · detector thread, wait-for graph | 1:20 | — | **1:20** |
| **D** | 10 · report, 13 · results | 0:40 | 0:40 | **1:20** |
| **E** | 11 · testing, 14 · limits, 15 · summary | 0:40 | 0:40 | **1:20** |

Speaker E also runs the live demo (slide 12, 1:50). The demo is not counted as presenting time.

Order: A → B → C → D → E → (E: demo) → D → E → everyone (Q&A)

---

## Part 1 — Presentation

### Slide 1 · Speaker A · Title — 0:10 (15 words)
Hello everyone. Our project is the Deadlock Detector. It finds deadlocks without changing the code.

### Slide 2 · Speaker A · The Problem — 0:30 (55 words)
Look at this bank program. Two threads move money. Thread 1 moves money from Alice to Bob. Thread 2 moves money from Bob to Alice. Thread 1 locks Alice first. Thread 2 locks Bob first. Now each one waits for the other. Nobody lets go. The program does not crash. It shows no error. It just stops.

### Slide 3 · Speaker A · Deadlock Basics — 0:25 (45 words)
A deadlock needs four conditions. First, mutual exclusion: one thread per lock. Second, hold and wait: each holds one, wants another. Third, no preemption: nobody can take a lock away. Fourth, circular wait: they wait in a loop. Remove one, and there is no deadlock.

### Slide 4 · Speaker A · Our Goal — 0:15 (25 words)
Our goal is simple. Find the deadlock. Show the threads, the locks, and the code lines. Do not change the code. Do not rebuild it.

### Slide 5 · Speaker B (นายศุภวิชญ์ ไชยานุศักดิ์) · Big Picture — 0:20 (37 words)
Here is the big picture. Our tool has four steps. First, catch every lock call. Second, record who holds and who waits. Third, check for a loop. Fourth, report it. Each step is one source file.

### Slide 6 · Speaker B (นายศุภวิชญ์ ไชยานุศักดิ์) · Step 1 — Catch — 0:30 (55 words)
Step one is catch. A mutex is a lock. Only one thread can hold it. LD_PRELOAD tells Linux to load our library first. So our lock function runs before the real one. It notes waiting. Then it calls the real lock. Then it notes got it. We catch three functions: lock, trylock and unlock.

### Slide 7 · Speaker B (นายศุภวิชญ์ ไชยานุศักดิ์) · Step 2 — Record — 0:30 (51 words)
Step two is record. We keep two tables. The holds table says who holds each lock. The waits table says who waits for which lock. The tables have a fixed size. Why? We cannot use malloc here. Malloc uses locks inside. That would call our code again, in a loop.

### Slide 8 · Speaker C · Step 3 — Detector Thread — 0:25 (46 words)
Step three is check. Our library starts its own thread. We call it the detector. It wakes up every 500 milliseconds. It copies the tables and looks for a loop. It never uses the program's locks. So it still works when the program is stuck.

### Slide 9 · Speaker C · Step 3 — Wait-for Graph — 0:55 (99 words)
We use a wait-for graph. Each circle is a thread. An arrow goes from the waiting thread. It points to the thread holding the lock. Thread 1 locks A. Thread 2 locks B. Thread 1 wants B. So we draw an arrow to Thread 2. Thread 2 wants A. So we draw an arrow back. Now the arrows make a loop. A loop means circular wait. That is a deadlock. A thread waits for only one lock at a time. So each circle has at most one arrow out. To find a loop, we just follow the arrows.

### Slide 10 · Speaker D · Step 4 — Report — 0:40 (69 words)
Step four is report. This is the real output. It says: deadlock detected, two threads in a loop. Thread 514 holds a lock from line 89. It is stuck at line 94. Thread 513 holds a lock from line 65. It is stuck at line 70. The hint tells us how to fix it. Always lock in the same order. We can also save the loop as a picture.

### Slide 11 · Speaker E · How We Tested — 0:40 (72 words)
Some tests must deadlock. Bank and sample webapp have two threads. Cycle3 has three threads. Two pairs has two loops at once. One must never deadlock. Lockorder must only warn. We also test hard cases. One has seventy threads in a single loop. We use a barrier, not sleep. So the deadlock happens every time. Each hard-case test fails if we remove its fix. Now let us show you the real thing.

---

## Part 2 — Live Demo

### Slide 12 · Speaker E · Live Demo — 1:50
If time is short, skip step 3 (slide 10 already shows the output).

| Step | Type | Say |
|---|---|---|
| 1 | `timeout 3 ./build/bank` | This is the bank program, with no tool. It stops. There is no message at all. Exit code 124 means timeout stopped it. |
| 2 | `timeout 3 env LD_PRELOAD=./build/libdetect.so ./build/bank` | Same command. We only add LD_PRELOAD. Now we get a report. Look at lines 65, 70, 89 and 94. |
| 3 | `DD_DOT_OUT=cycle.dot timeout 3 env LD_PRELOAD=./build/libdetect.so ./build/bank; dot -Tpng cycle.dot -o cycle.png` | We can save the loop as a picture. Red arrows mean waiting. |
| 4 | `timeout 15 env LD_PRELOAD=./build/libdetect.so ./build/no_deadlock` | This version locks in the same order. It finishes with exit code 0. There is no report. So there is no false alarm. |

Run from a path with no spaces (for example `~/dd`), or LD_PRELOAD will not load.

---

## Part 3 — After the Demo

### Slide 13 · Speaker D · Results — 0:40 (72 words)
Without our tool, every deadlock test just hangs. With our tool, we get a report every time. The no_deadlock test gives no report. No false alarm. Lockorder only gives a warning. We ran bank and sample webapp twenty times each. It found the deadlock every time. But the tool has a cost. One thread: lock plus unlock is about 31 times slower. Use it to find bugs. Do not leave it on.

### Slide 14 · Speaker E · Limits & Early Warning — 0:15 (27 words)
Our tool has limits. It only sees normal mutex locks. It finds a deadlock after it happens. But it also warns early about opposite lock orders.

### Slide 15 · Speaker E · Summary — 0:25 (46 words)
A deadlock is a loop in the wait-for graph. Our tool finds it with no code change. It shows the exact lines. The fix: lock in the same order. Our early warning helps you do this. Detect, prevent, avoid — we used all three ideas from class.

### Slide 16 · Everyone · Thank You / Q&A
Thank you. Any questions?
