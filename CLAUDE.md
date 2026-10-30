# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

An `LD_PRELOAD` library (`build/libdetect.so`) that interposes `pthread_mutex_lock` / `trylock` / `unlock`, builds a wait-for graph and reports deadlocks (`[DEADLOCK DETECTED]` on stderr, with `file:line`) in unmodified Linux/glibc dynamic binaries. Course project (OS, Deadlocks chapter); docs, comments and commit messages are in Thai.

## Platform and the path trap

- Linux or WSL2 only. `LD_PRELOAD` is a glibc dynamic-linker feature, so nothing works on native Windows/MinGW.
- **ld.so splits `LD_PRELOAD` on spaces and `:`.** If the absolute path of the repo contains a space, the library silently fails to load: the target still hangs but no report appears, which looks like "no deadlock found". Use the relative path `./build/libdetect.so` from the repo root, or work from a symlink to a space-free path. `tests/run_*.sh` use the relative path on purpose; `tools/playground.py` builds its root from `$PWD` + `argv[0]` so a symlink is not resolved back to the spaced path, and it refuses to run in detect mode if the path is bad.

## Commands

```bash
make                  # build/libdetect.so + all test programs (must build with no warnings)
make regress          # bash tests/run_regress.sh — edge cases (robust, errcheck, longcycle, condvar, trace); exit 0 = all pass
make demo             # bash tests/run_demo.sh — presentation demo, steps 1–4 with real pass/fail checks
bash tests/run_demo.sh --no-pause   # same, non-interactive (exit 0 = all steps pass); add --smoke for step 5
make bench            # tests/run_bench.sh -> writes tests/logs/bench.txt
make playground       # local web UI at http://127.0.0.1:8000 (python3 tools/playground.py [--port N])
make clean
```

Run a single test program directly (from the repo root):

```bash
timeout 5s env LD_PRELOAD=./build/libdetect.so ./build/bank
```

- Deadlocking tests (`bank`, `sample_webapp`, `two_pairs`, `cycle3`, `robust`, `errcheck`, `longcycle`) pass when exit is 124 (killed by `timeout`) **and** stderr contains `[DEADLOCK DETECTED]`. Without `LD_PRELOAD` they must still hang (exit 124) with no report.
- `no_deadlock` and `condvar` pass with exit 0 and no marker.
- `lockorder` passes with exit 0, exactly one `[LOCK ORDER INVERSION]` and no `[DEADLOCK DETECTED]`. Report text must never contain `WARNING` (regress counts it) or lines that `tools/playground.py` parses as a deadlock report (`Thread <tid>`, `holds`/`waiting mutex`, `Cycle:`).
- Timeouts the scripts use: 5 s for most deadlocking tests, 15 s for `no_deadlock` (runs ~2 s), 20 s for `condvar` and `longcycle` (a 64-thread report calls `addr2line` per line and takes seconds; too short a timeout cuts the report off).
- `run_regress.sh` has no per-case flag; to check one case, run its binary and compare against the expectations written in that script.
- Put `env LD_PRELOAD=...` after `timeout` so `timeout` itself is not preloaded.

Environment variables read by the library: `DD_VERBOSE=1` (debug log incl. table dumps), `DD_DOT_OUT=<file>` (Graphviz of current cycles), `DD_TRACE=<file>` (event-by-event timeline; format documented at the top of `src/trace.h`).

## Architecture (data flow across `src/`)

1. **`interpose.c`** — wrappers resolve the real functions via `dlsym(RTLD_NEXT)` (from the constructor; failure → `abort()`, never pretend the lock succeeded). Order per lock: `wait_begin` → real lock (the thread blocks here on deadlock) → `wait_end` (always, even on error) → `acquired` (on `rc == 0` or `EOWNERDEAD`). Unlock records `released` **before** the real unlock. Caller PC is captured with `__builtin_return_address(0)` inside the wrapper.
2. **`state.c`** — fixed-size arrays `waits[]` (one row per waiting thread) and `holds[]` (one row per held mutex), guarded by one mutex taken through `real_mutex_lock`. `released` only removes the row if the caller is the recorded holder (non-owner unlock → warn once, keep owner).
3. **`detector.c`** — a constructor starts a detector thread that polls every `DD_CHECK_INTERVAL_MS` (500 ms). It works even when every target thread is stuck because it never touches the target's mutexes.
4. **`graph.c`** — snapshot the tables under the state lock, build edges `waiter → holder`. Out-degree is ≤ 1 (a thread blocks on one mutex), so cycle search is a chain-walking white/gray/black DFS that finds **all** cycles in one pass (cap `DD_MAX_CYCLES` = 16; cycles longer than `DD_MAX_CYCLE_LEN` = 64 are marked `partial` and must never be drawn as closed). Detector dedups cycles by thread set against the previous round.
5. **`report.c`** — prints the report and writes DOT. `file:line` comes from `dladdr` (subtract `dli_fbase`) + `addr2line` via `popen`; correct only for PIE / `-g` targets, otherwise falls back to `at pc 0x…`.
6. **`trace.c`** — optional timeline for the playground. `DD_TRACING()` checks a hidden fd inline to keep the disabled cost near zero. It `unsetenv("DD_TRACE")` in the constructor because popen'd `addr2line` inherits `LD_PRELOAD` and would otherwise truncate the file.
7. **`log.c`** — `dd_logf` / `dd_dbgf` write with `write(2, …)`, never stdio.

`tools/playground.py` + `docs/playground.html`: a 127.0.0.1-only server that compiles and runs pasted C code (plain vs. detect mode) and parses the stderr report and `DD_TRACE` file for the UI. `docs/banker.html` is a standalone Banker's Algorithm simulator.

## Rules for code in the lock path

Wrapper and `state.c` code runs on the target program's threads:

- No `malloc`, `printf`/stdio or any locking except `real_*` functions. Inside the library never call `pthread_mutex_*` directly.
- `in_dd` (thread-local) prevents recursion; the detector thread sets it permanently so it never appears in the graph.
- Log formats must not use `%f`/`%g` (glibc `vsnprintf` may allocate).
- New pthread/libc calls check their return value (pthread functions return the error number, not `errno`).

## Test conventions

- Positive tests force the deadlock deterministically with `pthread_barrier` after the first lock — never `usleep`.
- `no_deadlock.c` must **not** get a barrier: both threads take the same first mutex, so a barrier would hang it. It orders locks by an `id` field (pointer comparison of distinct objects is UB) and runs ≥ 3 detector periods.
- Test programs are built with `-g -O0` because reports assert source lines; `bench.c` uses `-O2`.
- `DD_CHECK_INTERVAL_MS` (`src/common.h`, 500) is duplicated as `MIN_RUN_MS` = 3 × interval in `tests/no_deadlock.c`; `tools/playground.py` parses it from `common.h`. Change them together.

## Keep code and docs in sync

- `README.md`, `docs/overview.html` and `docs/playground.html` quote real output and line numbers (`bank.c:65/70/89/94`) and each has a Limitations section that must match the README. Changing `tests/bank.c` or the supported scope means updating all of them.
- Figures quoted in docs come from files in `tests/logs/`; regenerate the log rather than editing numbers by hand. `docs/design.md` records design decisions; `docs/fix-report.md` records each fix with its evidence.

## Repo conventions

- Comments and docs in Thai, technical terms in English.
- Commit subjects: `<type>(<file>): <what changed>` (`fix`, `test`, `feat`, `docs`), one item per commit.
