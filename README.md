# amos.oz — Arianna Method Operating System

amosOZ is a single-file operating system: canonical in C (`amosoz.c`, v0.4.0), with parity forms in Python and HTML/JS under `reffs/`. `amosoz.c` is llm.c-grade for an OS — the whole algorithm in one file, nothing hidden behind a build graph. C selftest: 60/60 (`make test`). The reference forms track the AMOS body, not the resonance field yet — they were built before the field and numbness existed.

Its shell treaty is its own contract, not POSIX. POSIX supplies the vocabulary; the semantics below are amosOZ's.

Dedicated to **Amos Oz** (עוז).

The **AMOS body** is per-process isolation, a preemptive scheduler, devices, `/proc`, fork/exec/wait, and mailbox IPC — the concrete OS. Field-by-field detail is in Architecture below.

The **AML resonance field** is hosted, not planned: it boots with the kernel, advances one step
per command, and the scheduler reads it to choose among equal-priority monads. An `.aml` program
is a monad — opened, scheduled a quantum at a time, charged for the statements it runs, and
collected by its parent when its program ends. C is the center.

## Quick Start

```bash
make && make test-all && ./amosoz
```

- `make test` — C selftest (60/60)  
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
| OZ / Meta | `oz`, `slots`, `organs`, `overhead`, `pulses`, `contracts`, `graft`, `shed`, `trace`, `replay`, `undo`, `spec`, `doctor`, `selftest`, `reset`, `fortune` |
| Field / AML | `field` (read the resonance field), `resonate <AML directive>` (a command IS a perturbation), `run <x.aml>` (an AML program becomes a monad, sliced `max_slice` statements per quantum — `slice <pid> N` tunes it), `mood <pid> [dim value]` (the monad's own weather — pain/tension/flow/warmth — weighed by the shared emergence) |
| Persist | `save`, `load` |

## Architecture (current foundation)

**AMOS (body)** — the concrete:
- Per-process: pid/ppid, cwd, open_fds[32], owned_blocks + mem_used/limit, cpu_time/limit/violations, signals + numbness, priority, max_slice/slice_used, mailbox[256], state (ready/running/blocked/zombie/stopped).
- Scheduler: priority + round-robin + time-slice preemption + hard cpu limits (violation + stop) + consolidated guarantee + ultimate force (shell/init) — never "none", always sets current_pid. Init reaps orphans only — a zombie whose parent is alive waits for that parent's `wait`.
- Memory: per-proc charge + hard limit enforced (strict, no global fallback).
- FS + devices (null/zero/full/random/urandom/tty + console/mem) with per-proc fds; unified read path (cat /dev/* delivers), write specials.
- Signals: delivery in tick, STOP/CONT/TERM/KILL + unblock on signal. **Numbness** (`numb` / `feel`) is the mask in this system's own register: a numbed signal is not lost, it stays pending until the monad feels again. Numbness is a property of the monad and outlives delivery; KILL and STOP pierce it.
- Fork: full clone of cwd/fds/limits/priority/numbness/owned + child reset (signals/cpu_time/violations/slice=0). Exec: name replace + reset signals/numbness/sleep/slice/violations, keeps fds/cwd.
- Rich /proc: top-level (uptime,meminfo,cpuinfo,version,self/status) + per-pid /status /fd /cpu /mem /stat /mailbox.
- Ring wake — the trace every command leaves (head/count, last 256).
- current_pid + shell_pid context everywhere (prompt, pwd, resolve, ownership, parent in spawn/fork/wait/exec, fg sticks via suppress).
- Auto time advance (main) + manual `tick`; fg suppresses auto-tick.

**OZ (field)** — the extension layer (organs, slots, pulses, contracts, wake provenance) plus the live AML field: `am_init` at boot, `am_step(0.1)` per command, `monad_choose` reading the field each tick, `.soma` persistence across runs, and `.aml` programs running as monads. A calm field reduces exactly to priority + round-robin, so the body is unchanged when the weather is still.

## Selftest (60/60)

```
make test
```

Covers core: boot, fs, permissions, processes (spawn/kill/fork/wait/exec), scheduler (slices, priorities, cpu limits, preemption), signals, devices, /proc, wake (ring), memory accounting, shell treaty.

## Parity Status

| File | Status |
|------|--------|
| `amosoz.c` | **canonical reference** (foundation complete) |
| `reffs/amosoz.py` + `reffs/amosoz.html` | parity maintained (reference forms) |

`reffs/` tracks `amosoz.c` for comparison. Where they diverge, `amosoz.c` is right.

## Current State & Roadmap

**Foundation phase (AMOS body)** — done:
- Per-process isolation + accounting (mem, fds, cpu time/limit/violations, signals+numbness).
- Scheduler with real preemption (slices + priorities + cpu hard limits) + guarantee no 'none'.
- Lifecycle: fork (full clone), exec (replace), wait, kill, blocking (sleep/pause).
- Signals + numbness (`numb` / `feel`) + delivery.
- Devices (null/zero/full/random/urandom/tty + unified read/write) + rich /proc/<pid>/* .
- Ring wake + IPC mailbox (send).
- Current context everywhere: current_pid + shell_pid for ownership/parent/prompt/cwd in all paths; strict no-fallback mem.
- All in **one self-contained C file**.

**Resonance:** AML field hosted and evolving, scheduler dissolved into it,
`.soma` persistence, `resonate` as a shell handle, `.aml` programs running as monads.

Each monad also carries its own thin slice of that weather (`mood`: pain / tension / flow /
warmth). Its own agitation argues for the CPU, its own flow yields; the shared emergence sets
how loudly anyone may argue. A mood fades each tick and discharges when served, so it bends the
scheduler without jamming it — and an empty mood reduces the scheduler to round-robin exactly.

An `.aml` monad is time-sliced: the canon gained resumable execution
(`am_program_open` / `step` / `close`), so a monad runs `max_slice` statements per quantum and
lives as long as its program rather than as long as one command. It is charged for the statements
it actually runs, and what it moves in the shared field accumulates into its own mood.

**Next:**
- Suspending *inside* a loop. A `while` still runs all of its iterations within one step, because
  the block state lives on the C stack — lifting it into the program handle is canon work.
- Go layer with goroutines *around* the C kernel. Threads stay outside the kernel while the AML
  field is an unlocked singleton — a threaded monad would race the main loop's `am_step`.

## Design Principles

> amosOZ's shell treaty sets its own rules; POSIX supplies the vocabulary, not the compliance target.  
> Every command leaves a trace — ring-buffered, last 256.  
> OZ begins where an extension has to prove itself: organs, pulses, contracts, wake provenance.  
> Foundation first.

## License

See repository license.
