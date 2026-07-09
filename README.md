# amos.oz — Arianna Method Operating System

**The most atomic way to build a working OS-like environment from scratch.**

amosOZ is a single-file OS environment in three forms: C, HTML/JS, Python. Not **Treaty-compatible** with Unix userland semantics.

**Canonical:** `amosoz.c` **v0.4.x** — reference implementation (llm.c-grade for OS).  
**Parity:** `amosoz.py` + `amosoz.html` **v0.4.x** — triple parity (selftest ~50/50).

Dedicated to **Amos Oz** (עוז). 

This is the **AMOS body** (foundation): per-process isolation (memory, fds, cwd, cpu accounting), scheduler with priorities + time slices + hard cpu limits, signals with masks + delivery, devices, rich /proc, fork/exec/wait, blocking, IPC mailbox.

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

New in this foundation phase: per-process fds/cwd/memory/cpu, signals with masks, blocking, fork+exec with proper cloning/reset, ring ledger, rich /proc/<pid>/{status,fd,cpu,mem,stat,mailbox}.

## Architecture (current foundation)

**AMOS (body)** — the concrete:
- Per-process: cwd, open_fds[32], owned memory blocks, cpu_time + cpu_limit + violations, signals + sigmask, priority, slice, mailbox.
- Scheduler: priority + time-slice preemption + hard cpu limits (stops process when limit hit).
- Memory: owned + limit enforced.
- FS + devices with per-proc fds.
- Signals with masks + delivery (STOP/CONT/KILL/TERM etc.).
- Fork (clones state), exec (image replace + reset), wait, blocking (sleep/pause on signal).
- Rich /proc/<pid>/{status,fd,cpu,mem,stat,mailbox}.
- Ring ledger (last 256 always preserved).
- Auto time advance + manual tick.

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
| `amosoz.py` + `amosoz.html` | parity maintained |

The point is the **single C file** as the complete, self-contained algorithm. Py/HTML are for comparison only.

## Current State & Roadmap

**Foundation phase (AMOS body)** — done:
- Per-process isolation + accounting (mem, fds, cpu time/limit/violations, signals+mask).
- Scheduler with real preemption (slices + priorities + cpu hard limits).
- Lifecycle: fork (full clone), exec (replace), wait, kill, blocking (sleep/pause).
- Signals with masks + delivery.
- Devices + rich /proc.
- Ring ledger + IPC mailbox.
- All in **one self-contained C file**.

**Next (still foundation, no resonance yet):**
- Further hardening (more limits, better devices, signals masks/delivery).
- Optimization of the body (without leaving single-file constraint yet).
- Then: Go layer with goroutines *around* the C kernel (concurrency, drivers, multiple "users").

Resonance / OZ layer (θ, actors, AML) — only after the concrete is solid.

## Design Principles

> "Compatibility is a treaty, not obedience."  
> "Every command leaves a trace." (now ring-buffered, last 256)  
> "OZ begins where extension becomes accountable."  
> C is the center. Foundation first.

## License

See repository license.
