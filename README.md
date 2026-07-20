# amos.oz — Arianna Method Operating System

**The most atomic way to build a working OS-like environment from scratch.**

amosOZ is a single-file OS environment in three forms: C, HTML/JS, Python. Not **Treaty-compatible** with Unix userland semantics.

**Canonical:** `amosoz.c` **v0.4.x** — reference implementation (llm.c-grade for OS).  
**Parity:** `reffs/amosoz.py` + `reffs/amosoz.html` **v0.4.x** — triple parity (selftest ~50/50). Reference forms; C is canonical.

Dedicated to **Amos Oz** (עוז). 

This is the **AMOS body** (foundation): per-process isolation (cwd, open_fds[32], owned memory blocks + limits, cpu_time/limit/violations, signals + sigmask, priority, max_slice, mailbox), scheduler (priority + slice preemption + hard cpu limits + no-'none' guarantee), current_pid + shell_pid context, signals, devices, rich /proc, fork (full clone)/exec (replace+reset), wait, blocking, IPC via mailbox/send.

Resonance / OZ layer (θ, actors, AML hooks) — later, after foundation is solid. C is the center.

## Quick Start

```bash
make && make test-all && ./amosoz
```

- `make test` — C selftest (~50/50)  
- `make test-py` — Python parity  
- `make test-html` — headless HTML/JS parity  
- `make test-parity` — all three in one shot  
- `make test-shell` — shell treaty smoke (C)  
- `spec` / `doctor` — inside the shell

## Shell Treaty

```bash
echo hi > /tmp/x.txt          # >
echo more >> /tmp/x.txt       # >>
echo piped | grep piped       # |
cat < /etc/hostname           # <
which grep                    # PATH → /bin/grep
exec /home/user/hello.amos world
fortune oz                    # Amos Oz quotes from /usr/share/amosoz/
```

Operators: `>`, `>>`, `|`, `<`  
Scripts: `#!/amossh`, `.amos`, `$@`, `$1`…`$9`  
`/bin/*` stubs: `__builtin__` → kernel command

## Commands (current foundation)

Core OS primitives (AMOS body):

| Category | Commands |
|----------|----------|
| System | `uname`, `version`, `boot`, `hw`, `devices`, `date`, `motd`, `whoami`, `uptime`, `hostname`, `id`, `dmesg`, `current` |
| Filesystem | `pwd`, `cd`, `ls`, `cat`, `touch`, `write`, `append`, `rm`, `mkdir`, `rmdir`, `mv`, `cp`, `chmod`, `stat`, `tree`, `ln`, `find`, `open`, `close`, `readfd`, `writefd`, `fds` |
| Memory | `mem`, `mmap`, `alloc`, `free` (per-proc accounting + limits) |
| Process | `ps` (with CpuTime/CpuLim/Mem/Prio/Slice/FDs), `run`, `fork`, `exec`, `kill` (incl. `-s`), `tick`, `wait`, `sleep`, `pause`, `yield`, `fg`, `jobs` (stubs) |
| Scheduler / Resources | `nice`, `slice`, `limit`, `climit` (cpu + mem hard limits + violations) |
| Shell / IPC | `echo`, `env`, `set`, `unset`, `export`, `source`, `history`, `which`, `exec`, `test`, `send` (mailbox IPC) |
| Logic / Syscall | `true`, `false`, `syscall` |
| OZ / Meta | `oz`, `slots`, `modules`, `overhead`, `hooks`, `contracts`, `trace`, `replay`, `undo`, `spec`, `doctor`, `selftest`, `reset`, `fortune` |
| Persist | `save`, `load` |

New in this foundation phase: per-process fds/cwd/memory/cpu + strict ownership (no global fallback), current_pid + shell_pid for all context/parent/prompt/cwd, signals with masks, blocking, fork (clones fds/cwd/limits/sigmask/owned + resets child), exec (reset signals/mask/slice/violations), unified devices, ring ledger, rich /proc + per-pid files, scheduler guarantee (never none).

## Architecture (current foundation)

**AMOS (body)** — the concrete:
- Per-process: pid/ppid, cwd, open_fds[32], owned_blocks + mem_used/limit, cpu_time/limit/violations, signals + sigmask, priority, max_slice/slice_used, mailbox[256], state (ready/running/blocked/zombie/stopped).
- Scheduler: priority + round-robin + time-slice preemption + hard cpu limits (violation + stop) + consolidated guarantee + ultimate force (shell/init) — never "none", always sets current_pid. Init reaps zombies.
- Memory: per-proc charge + hard limit enforced (strict, no global fallback).
- FS + devices (null/zero/full/random/urandom/tty + console/mem) with per-proc fds; unified read path (cat /dev/* delivers), write specials.
- Signals: masks (KILL/STOP unmaskable), delivery in tick, STOP/CONT/TERM/KILL + unblock on signal.
- Fork: full clone of cwd/fds/limits/priority/sigmask/owned + child reset (signals/cpu_time/violations/slice=0). Exec: name replace + reset signals/mask/sleep/slice/violations, keeps fds/cwd.
- Rich /proc: top-level (uptime,meminfo,cpuinfo,version,self/status) + per-pid /status /fd /cpu /mem /stat /mailbox.
- Ring ledger (head/count, last 256).
- current_pid + shell_pid context everywhere (prompt, pwd, resolve, ownership, parent in spawn/fork/wait/exec, fg sticks via suppress).
- Auto time advance (main) + manual `tick`; fg suppresses auto-tick.

**OZ (field)** — the extension layer (modules, slots, hooks, contracts, ledger provenance) — built on top of the solid AMOS body. Not mixed in yet.

Purpose: single-file, self-contained, llm.c-grade minimal OS. C is the center. Everything verifiable in one file. Go/goroutines layer comes *around* it later.

## Selftest (~50/50)

```
make test
```

Covers core: boot, fs, permissions, processes (spawn/kill/fork/wait/exec), scheduler (slices, priorities, cpu limits, preemption), signals, devices, /proc, ledger (ring), memory accounting, shell treaty.

## Parity Status

| File | Status |
|------|--------|
| `amosoz.c` | **canonical reference** (foundation complete) |
| `reffs/amosoz.py` + `reffs/amosoz.html` | parity maintained (reference forms) |

The point is the **single C file** as the complete, self-contained algorithm. Py/HTML are for comparison only.

## Current State & Roadmap

**Foundation phase (AMOS body)** — done:
- Per-process isolation + accounting (mem, fds, cpu time/limit/violations, signals+mask).
- Scheduler with real preemption (slices + priorities + cpu hard limits) + guarantee no 'none'.
- Lifecycle: fork (full clone), exec (replace), wait, kill, blocking (sleep/pause).
- Signals with masks + delivery.
- Devices (null/zero/full/random/urandom/tty + unified read/write) + rich /proc/<pid>/* .
- Ring ledger + IPC mailbox (send).
- Current context everywhere: current_pid + shell_pid for ownership/parent/prompt/cwd in all paths; strict no-fallback mem.
- All in **one self-contained C file**.

**Next (still foundation, no resonance yet):**
- Optimization of the body (without leaving single-file constraint yet).
- Go layer with goroutines *around* the C kernel (concurrency for procs, drivers, multiple users, channel IPC over the single-file core).

Resonance / OZ (θ, actors, AML) only after the concrete body + Go layer is solid.

Resonance / OZ layer (θ, actors, AML) — only after the concrete is solid.

## Design Principles

> "Compatibility is a treaty, not obedience."  
> "Every command leaves a trace." (now ring-buffered, last 256)  
> "OZ begins where extension becomes accountable."  
> C is the center. Foundation first.

## License

See repository license.
