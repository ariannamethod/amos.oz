# amos.oz — Arianna Method Operating System

**The most atomic way to build a working OS-like environment from scratch.**

amosOZ is a single-file OS environment in three forms: C, HTML/JS, Python. Not **Treaty-compatible** with Unix userland semantics.

**Canonical:** `amosoz.c` **v0.4.x** — reference implementation (llm.c-grade for OS).  
**Parity:** `reffs/amosoz.py` + `reffs/amosoz.html` **v0.4.x** — triple parity (the C selftest is 59/59; the reference forms predate the field and numbness). Reference forms; C is canonical.

Dedicated to **Amos Oz** (עוז). 

This is the **AMOS body** (foundation): per-process isolation (cwd, open_fds[32], owned memory blocks + limits, cpu_time/limit/violations, signals + numbness, priority, max_slice, mailbox), scheduler (priority + slice preemption + hard cpu limits + no-'none' guarantee), current_pid + shell_pid context, signals, devices, rich /proc, fork (full clone)/exec (replace+reset), wait, blocking, IPC via mailbox/send.

The AML resonance field is **hosted, not planned**: it boots with the kernel, advances one step
per command, and the scheduler reads it to choose among equal-priority procs. An `.aml` program
runs as a monad — spawned, charged, reaped. C is the center.

## Quick Start

```bash
make && make test-all && ./amosoz
```

- `make test` — C selftest (59/59)  
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
| Process | `ps` (with CpuTime/CpuLim/Mem/Prio/Slice/FDs), `run`, `fork`, `exec`, `kill` (incl. `-s`), `signal`, `numb` / `feel` (numbness — which signals a monad cannot feel), `tick`, `wait`, `sleep`, `pause`, `yield`, `fg`, `jobs` (stubs) |
| Scheduler / Resources | `nice`, `slice`, `limit`, `climit` (cpu + mem hard limits + violations) |
| Shell / IPC | `echo`, `env`, `set`, `unset`, `export`, `source`, `history`, `which`, `exec`, `test`, `send` (mailbox IPC) |
| Logic / Syscall | `true`, `false`, `syscall` |
| OZ / Meta | `oz`, `slots`, `modules`, `overhead`, `hooks`, `contracts`, `trace`, `replay`, `undo`, `spec`, `doctor`, `selftest`, `reset`, `fortune` |
| Field / AML | `field` (read the resonance field), `resonate <AML directive>` (a command IS a perturbation), `run <x.aml>` (an AML program runs as a monad), `mood <pid> [dim value]` (the monad's own weather — pain/tension/flow/warmth — weighed by the shared emergence) |
| Persist | `save`, `load` |

New in this foundation phase: per-process fds/cwd/memory/cpu + strict ownership (no global fallback), current_pid + shell_pid for all context/parent/prompt/cwd, signals with numbness, blocking, fork (clones fds/cwd/limits/numbness/owned + resets child), exec (reset signals/numbness/slice/violations), unified devices, ring ledger, rich /proc + per-pid files, scheduler guarantee (never none).

## Architecture (current foundation)

**AMOS (body)** — the concrete:
- Per-process: pid/ppid, cwd, open_fds[32], owned_blocks + mem_used/limit, cpu_time/limit/violations, signals + numbness, priority, max_slice/slice_used, mailbox[256], state (ready/running/blocked/zombie/stopped).
- Scheduler: priority + round-robin + time-slice preemption + hard cpu limits (violation + stop) + consolidated guarantee + ultimate force (shell/init) — never "none", always sets current_pid. Init reaps zombies.
- Memory: per-proc charge + hard limit enforced (strict, no global fallback).
- FS + devices (null/zero/full/random/urandom/tty + console/mem) with per-proc fds; unified read path (cat /dev/* delivers), write specials.
- Signals: delivery in tick, STOP/CONT/TERM/KILL + unblock on signal. **Numbness** (`numb` / `feel`) is the mask in this system's own register: a numbed signal is not lost, it stays pending until the monad feels again. Numbness is a property of the monad and outlives delivery; KILL and STOP pierce it.
- Fork: full clone of cwd/fds/limits/priority/numbness/owned + child reset (signals/cpu_time/violations/slice=0). Exec: name replace + reset signals/numbness/sleep/slice/violations, keeps fds/cwd.
- Rich /proc: top-level (uptime,meminfo,cpuinfo,version,self/status) + per-pid /status /fd /cpu /mem /stat /mailbox.
- Ring ledger (head/count, last 256).
- current_pid + shell_pid context everywhere (prompt, pwd, resolve, ownership, parent in spawn/fork/wait/exec, fg sticks via suppress).
- Auto time advance (main) + manual `tick`; fg suppresses auto-tick.

**OZ (field)** — the extension layer (modules, slots, hooks, contracts, ledger provenance) plus the live AML field: `am_init` at boot, `am_step(0.1)` per command, `proc_field_select` reading the field each tick, `.soma` persistence across runs, and `.aml` programs running as monads. A calm field reduces exactly to priority + round-robin, so the body is unchanged when the weather is still.

Purpose: single-file, self-contained, llm.c-grade minimal OS. C is the center. Everything verifiable in one file. Go/goroutines layer comes *around* it later.

## Selftest (59/59)

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
- Per-process isolation + accounting (mem, fds, cpu time/limit/violations, signals+numbness).
- Scheduler with real preemption (slices + priorities + cpu hard limits) + guarantee no 'none'.
- Lifecycle: fork (full clone), exec (replace), wait, kill, blocking (sleep/pause).
- Signals + numbness (`numb` / `feel`) + delivery.
- Devices (null/zero/full/random/urandom/tty + unified read/write) + rich /proc/<pid>/* .
- Ring ledger + IPC mailbox (send).
- Current context everywhere: current_pid + shell_pid for ownership/parent/prompt/cwd in all paths; strict no-fallback mem.
- All in **one self-contained C file**.

**Resonance — done, not pending:** AML field hosted and evolving, scheduler dissolved into it,
`.soma` persistence, `resonate` as a shell handle, `.aml` programs running as monads.

Each monad also carries its own thin slice of that weather (`mood`: pain / tension / flow /
warmth). Its own agitation argues for the CPU, its own flow yields; the shared emergence sets
how loudly anyone may argue. A mood fades each tick and discharges when served, so it bends the
scheduler without jamming it — and an empty mood reduces the scheduler to round-robin exactly.

**Next:**
- Budgeted `.aml` execution — a monad currently consumes its quantum whole. Real time-slicing
  needs an `am_exec_step` / `am_resume` API the AML canon does not have yet.
- Go layer with goroutines *around* the C kernel. Threads stay outside the kernel while the AML
  field is an unlocked singleton — a threaded monad would race the main loop's `am_step`.

## Design Principles

> "Compatibility is a treaty, not obedience."  
> "Every command leaves a trace." (now ring-buffered, last 256)  
> "OZ begins where extension becomes accountable."  
> C is the center. Foundation first.

## License

See repository license.
