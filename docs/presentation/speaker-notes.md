# Speaker Notes — Deadlock Detector

Total 8:13 (6:23 presenting + 1:50 demo). Pace: about 110 words per minute.
Speakers are labelled **A–E**. In the slides, the label is in the footer, and **N** shows these notes.

| Speaker | Slides | Before the demo | After the demo | Presenting time |
|---|---|---|---|---|
| **A** — นายภูตะวัน ทรัพย์เกิด 6730300884 | 1–4 · title, problem, Coffman, goal · 15 · summary | 1:23 | 0:25 | **1:48** |
| **B** — นายศุภวิชญ์ ไชยานุศักดิ์ 6730300591 | 5–7 · big picture, LD_PRELOAD, tables | 1:20 | — | **1:20** |
| **C** — นายชญานิน เอี่ยมสกุล 6730300094 | 8–9 · detector thread, wait-for graph | 1:20 | — | **1:20** |
| **D** — นายธรรศธรณ์ เจียมตน 6730300779 | 10 · report, 13 · results | 0:40 | 0:40 | **1:20** |
| **E** — นายพัฒนเดช มะหิเมือง 6730300850 | 11 · testing, 14 · limits | 0:20 | 0:15 | **0:35** |

Speaker E also runs the live demo (slide 12, 1:50). The demo is not counted as presenting time.

Order: A → B → C → D → E → (E: demo) → D → E → A → everyone (Q&A)

---

## Part 1 — Presentation

### Slide 1 · Speaker A (นายภูตะวัน ทรัพย์เกิด) · Title — 0:10 (15 words)
Hello everyone. Our project is the Deadlock Detector. It finds deadlocks without changing the code.

### Slide 2 · Speaker A (นายภูตะวัน ทรัพย์เกิด) · The Problem — 0:30 (55 words)
Look at this bank program. Two threads move money. Thread 1 moves money from Alice to Bob. Thread 2 moves money from Bob to Alice. Thread 1 locks Alice first. Thread 2 locks Bob first. Now each one waits for the other. Nobody lets go. The program does not crash. It shows no error. It just stops.

### Slide 3 · Speaker A (นายภูตะวัน ทรัพย์เกิด) · Deadlock Basics — 0:28 (51 words)
A deadlock needs four conditions. First, mutual exclusion: one thread per lock. Second, hold and wait: each holds one, wants another. Third, no preemption: nobody can take a lock away. Fourth, circular wait: they wait in a loop. Remove one, and there is no deadlock. Our tool looks for the loop.

### Slide 4 · Speaker A (นายภูตะวัน ทรัพย์เกิด) · Our Goal — 0:15 (25 words)
Our goal: find the deadlock. Show the threads, locks, and code lines. No code change. No rebuild. Now Frank will show how our tool works.

### Slide 5 · Speaker B (นายศุภวิชญ์ ไชยานุศักดิ์) · Big Picture — 0:20 (35 words)
Thank you. I'm Frank. Our tool is one small library: libdetect.so. It has four steps. One: catch every lock call. Two: record who holds and who waits. Three: check for a loop. Four: report it.

### Slide 6 · Speaker B (นายศุภวิชญ์ ไชยานุศักดิ์) · Step 1 — Catch — 0:30 (55 words)
Step one: catch. A mutex is like a door lock. Only one thread can hold it. LD_PRELOAD is a Linux setting. It loads our library first. So our function runs before the real lock. It writes down, "waiting." It calls the real lock. Then it writes down, "got it." We catch lock, trylock, and unlock.

### Slide 7 · Speaker B (นายศุภวิชญ์ ไชยานุศักดิ์) · Step 2 — Record — 0:30 (53 words)
Step two: record. The holds table shows who holds each lock. The waits table shows who waits for which lock. Why a fixed size? We cannot use malloc here. Malloc uses locks inside. So it would call our code again. That makes an endless loop. Next, Chayanin shows how we find the loop.

### Slide 8 · Speaker C (นายชญานิน เอี่ยมสกุล) · Step 3 — Detector Thread — 0:25 (46 words)
Step three is check. Our library starts its own thread. We call it the detector. It wakes up every 500 milliseconds. It copies the tables and looks for a loop. It never uses the program's locks. So it still works when the program is stuck.

### Slide 9 · Speaker C (นายชญานิน เอี่ยมสกุล) · Step 3 — Wait-for Graph — 0:55 (101 words)
We use a wait-for graph. Each circle is a thread. An arrow goes from the waiting thread. It points to the thread holding the lock. Thread 1 locks Alice. Thread 2 locks Bob. Thread 1 wants Bob. So we draw an arrow to Thread 2. Thread 2 wants Alice. So we draw an arrow back. Now the arrows make a loop. This circular wait is a deadlock. A thread waits for only one lock at a time. So each circle has at most one arrow out. To find a loop, we just follow the arrows. Now ___ will explain the report.

### Slide 10 · Speaker D (นายธรรศธรณ์ เจียมตน) · Step 4 — Report — 0:40 (71 words)
Step four is report. This is real output. It says: deadlock detected, two threads in a loop. 514 and 513 are thread IDs from Linux. Thread 514 holds a lock from line 89. It is stuck at line 94. Thread 513 holds a lock from line 65. It is stuck at line 70. The hint shows the fix. Always lock in the same order. Now ___ will show how we tested.

### Slide 11 · Speaker E (นายพัฒนเดช มะหิเมือง) · How We Tested — 0:20 (39 words)
We tested three kinds of programs. Some must deadlock, with two, three, or seventy threads. Some must never deadlock. One must only warn. We use a barrier, not sleep, so the deadlock happens every time. Let us show you.

---

## Part 2 — Live Demo

### Slide 12 · Speaker E (นายพัฒนเดช มะหิเมือง) · Live Demo — 1:50
If time is short, skip step 3 (slide 10 already shows the output).

| Step | Type | Say |
|---|---|---|
| 1 | `timeout 3 ./build/bank` | This is the bank program, with no tool. It stops. There is no message at all. Exit code 124 means timeout stopped it. |
| 2 | `timeout 3 env LD_PRELOAD=./build/libdetect.so ./build/bank` | Same command. We only add LD_PRELOAD. It still hangs, but now we know why. Look at lines 65, 70, 89 and 94. |
| 3 | `DD_DOT_OUT=cycle.dot timeout 3 env LD_PRELOAD=./build/libdetect.so ./build/bank; dot -Tpng cycle.dot -o cycle.png` | We can save the loop as a picture. Red arrows mean waiting. |
| 4 | `timeout 15 env LD_PRELOAD=./build/libdetect.so ./build/no_deadlock` | This version locks in the same order. It always locks Alice before Bob. It finishes with exit code 0. There is no report. So there is no false alarm. Now ___ will show our results. |

Run from a path with no spaces (for example `~/dd`), or LD_PRELOAD will not load.

---

## Part 3 — After the Demo

### Slide 13 · Speaker D (นายธรรศธรณ์ เจียมตน) · Results — 0:40 (72 words)
Without our tool, every deadlock test just hangs. With our tool, we get a report every time. The no_deadlock test gives no report. No false alarm. Lockorder only gives a warning. In twenty runs, it found the deadlock every time. But the tool has a cost. Each lock and unlock costs about 70 nanoseconds more. That is 31 times slower. Use it for testing only. Now ___ will talk about the limits.

### Slide 14 · Speaker E (นายพัฒนเดช มะหิเมือง) · Limits & Early Warning — 0:15 (28 words)
Now, the limits. It works only on Linux, with normal mutex locks. It finds a deadlock after it happens. But it also warns early about opposite lock orders.

### Slide 15 · Speaker A (นายภูตะวัน ทรัพย์เกิด) · Summary — 0:25 (46 words)
A deadlock is a loop in the wait-for graph. Our tool finds it with no code change. It shows the exact lines. The fix: lock in the same order. Our early warning helps you do this. Detect, prevent, avoid — we used all three ideas from class.

### Slide 16 · Everyone · Thank You / Q&A
Thank you. Any questions?

---

## Possible Questions (Q&A)

Short answers to prepare. The suggested speaker knows that part best.

**Q: Can it find a livelock?** · E
No. In a livelock, threads keep running. Nobody waits, so there is no loop in our graph.

**Q: Why only mutex? Why not semaphore?** · E
A mutex has one owner. So we know who waits for whom. A semaphore has no owner. Anyone can release it. So we cannot draw the arrow.

**Q: How do you know it is really a deadlock?** · C
Each lock has one owner. So a loop means everyone waits forever. All four Coffman conditions are true. We do not guess from time.

**Q: Did you write LD_PRELOAD?** · B
No. LD_PRELOAD is part of Linux. We wrote the library, libdetect.so. LD_PRELOAD just loads it first.

**Q: How much slower is the program?** · D
About 70 nanoseconds more per lock and unlock. Every lock updates our tables. So use it for testing only.

**Q: Can you turn it off?** · D
Yes. Just run the program without LD_PRELOAD. Then the cost is zero. No rebuild is needed.

**Q: Did you really test it?** · E
Yes. We removed each fix, one at a time. Each time, its own test failed.

**Q: Does it stop the deadlock?** · E
No. It finds the deadlock after it happens. But it warns early about opposite lock orders.
