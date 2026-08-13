# AMOSLOG

Reproducible log of changes to amosOZ. Each entry records **what** changed, **why**,
and **how it was verified** — decisions and evidence, not process. Newest first.

## Roadmap (open — not yet done)

- **Bound the quantum, not the loop (partly done 08-13).** A slice is timed after the fact and a
  stalling monad is stopped. Still open: the only real source of a stall is a blocking directive
  inside a slice, and the watchdog reacts rather than prevents. Preventing it means the language
  offering a non-blocking form of every directive that can wait — `CHANNEL TRY` is the first.

---

## 2026-08-13 — `sleep` read the command's own name, and put a stranger to bed

The tail of wave 1. Two defects in four lines, both visible in one probe:

```
sleep 5                      →  "no running process to sleep"
run drifter; tick; sleep 5   →  "PID 3 blocked for 0 ticks"
```

`atoi(argv[0])` read the **command name** — `atoi("sleep")` is 0 — so the user's argument was
never looked at, and the count was always zero. And it blocked the first row in state `running`,
which is the scheduler's pick: `sleep` at the prompt put **someone else** to sleep, or refused
outright when the shell was not the one selected.

Now it blocks `actor_pid` and reads `argv[1]`; a missing or non-positive count is refused
instead of silently sleeping zero.

### Verification
- `sleep 5` → `PID 2 blocked for 5 ticks`, and `ps` shows the shell `blocked`. With a live
  neighbour it still takes the shell, not `drifter`. `sleep` and `sleep abc` are both refused.
- Four treaty cases; the same suite against a build of `12cd4ea` fails with
  `sleep did not block the actor for the asked count`.
- Selftest **60/60**, 0 FAIL; `html_selftest` **43/43**; ASan **0** on the sleep path.

**What the probe found that is not mine, measured rather than argued.** The actor does *not*
sleep for the count it asked: `sleep 5` wakes it after **2** ticks. The cause is the never-none
rescue at `amosoz.c:1394` — its last cascade level grabs any monad that is neither zombie nor
stopped and forces it to `ready`, and a *blocked* monad qualifies. The occupancy guarantee wakes
the sleeper. So no duration case was written: it would have asserted a lie. This is Sol's
blocker #3 with a number attached, and it is the next wave — an explicit IDLE/silence locus
rather than a fallback that resurrects hard state.

---

## 2026-08-13 — Sol's audit, wave 1: the actor is not the scheduler's pick

An independent read-only audit of `main@030eba3` by Sol (GPT-5.6) landed. Its central verdict is
not a bug list: `Monad` / `current_pid` conflated four different things — whom the scheduler
chose, who authored a shell command, who owns resources, and what backend a monad actually has.
This wave closes the first of them, in the order the handoff asked for.

**The defect in one line, and it is user-visible:** `cd /tmp` was invisible to the very next
command. Measured, three cold runs: `cd /tmp ; pwd` → `/`, while `cd /tmp ; true ; pwd` → `/tmp`.
No second monad needed. Both `chdir` and `cmd_pwd` addressed the monad at `current_pid`; between
two commands the main loop ticks, the scheduler picks someone else, so the shell wrote its cwd
into one monad and read it back from another. The interactive user's working directory belonged
to whoever the scheduler happened to select.

- `current_pid` → **`selected_pid`** (whom the scheduler chose) and a new **`actor_pid`** (on
  whose behalf a command runs; the shell by default, changed by `fg`).
- 28 sites moved to the actor: every `kernel_syscall` path, `fs_resolve`, cwd, fds, memory
  ownership, `fork` / `run` / `exec` parenthood, `wait`. The scheduler keeps its own field in
  `monad_tick`, `monad_choose`, `cmd_tick`, `cmd_current`.

### Verification
- The two halves are proven separately. The **rename** is byte-identical: a 47-command scenario
  frozen first at md5 `c035ef4ab2c384e00242f04eb9b7e9e7` reproduces exactly after
  `current_pid` → `selected_pid`. Behaviour moves only in the second half, when the actor exists.
- Every one of the 18 differing lines after the split is the identity fix, each in the right
  direction: `pwd` after `cd` is `/tmp`; `forked pid 5 from 3` becomes `from 2`; `alloc` charges
  the shell instead of the scheduler's pick. **`fork` at the prompt used to clone a stranger** —
  whichever monad the scheduler had selected — which is not cosmetic.
- Three cases added to the shell treaty (cd survives, memory charged to the shell, fork clones
  the shell). The same suite run against a build of the pre-fix `amosoz.c` fails with
  `cd does not survive to the next command`.
- **Two selftests Sol found were literal `CHECK(..., 1)`** — `process_kill` and `ps_has_mem`,
  both marked "simplified for now". My `60/60` has been carrying two no-ops all along. They are
  now real (`monad_kill_zombies`, `ps_reports_memory`) and both fail on a build where the thing
  they name is broken.
- Her measurement corrections, re-checked here: the command table holds **105 names / 104
  handlers**, not the 127 I had claimed from a sloppy `grep`. Coverage: shell treaty exercises
  **20/104** handlers, with the selftest **28/104**. The frozen-baseline method is a lock on a
  known path, not broad evidence.
- `make` 0 errors; selftest **60/60**, 0 FAIL; shell treaty **ALL PASSED**; `html_selftest`
  **43/43**; ASan **0** on the scenario.

**Still open from wave 1, named rather than quietly skipped:** `cmd_sleep` parses `argv[0]` and
blocks an arbitrary running row for zero ticks. Same blocker, separate probe.

---

## 2026-08-13 — The roadmap item was wrong; the quantum gets a watchdog instead

The open item said a long `while` inside a monad freezes the system, so the canon should make
loops suspendable. **Measured, that is false.** AML caps a loop at 10000 iterations
(`core/ariannamethod.c:6079`) and the interpreter is fast: 100, 2000 and 9000 iterations all
come in at **0.01 s**. Arithmetic cannot stretch a quantum. I had called that gate live twice —
first framed as tick starvation, then as wall time — and it was neither.

**The reachable hazard is a blocking directive inside the body, not the loop itself.** Three
empty `CHANNEL READ`s in a loop: **3.90 s**. The same loop on `CHANNEL TRY`: **0.02 s**. And
suspendable loops would not fix it — yielding happens *between* iterations, while the freeze sits
*inside* one, asleep in `nanosleep`. There is nothing to yield.

So the roadmap item is dropped and replaced by what the measurement actually asks for. The kernel
cannot preempt a slice — control is inside the interpreter until the statement returns — so the
quantum is timed **after the fact**:

- `monad_run_slice` measures its own wall duration (`CLOCK_MONOTONIC`; `clock()` would see
  nothing, since a sleeping thread burns no CPU — a lesson already paid for in the canon).
- A slice past `MAX_SLICE_STALL_MS` (250) increments `stall_count` and the monad is **stopped**.
  The harm already happened; letting it take the next turn repeats it.
- `/proc/<pid>/status` reports `Stall: <ms> (<count> over <threshold> ms)`.

### Verification
- Blocking read in a loop → `State: stopped`, `Stall: 3781 ms (1 over 250 ms)`. The same program
  on `CHANNEL TRY` → `State: zombie`, `Stall: 0 ms (0 over 250 ms)`: the watchdog catches a stall,
  not merely any monad.
- **Falsified two ways.** A scratch build with the threshold raised to 99000 ms lets the same
  input run to completion (`zombie`, `0 over 99000 ms`), and the same treaty suite run against
  that build fails with `a stalling slice was not counted`.
- Both cases are in the shell treaty (`runtime/probe_stall.aml`), the clean one reusing
  `probe_empty.aml`. Selftest **60/60**, 0 FAIL — the 4-second probe deliberately lives in the
  treaty, not in `make test`. `html_selftest` **43/43**; ASan **0** on the watchdog path.

---

## 2026-08-10 — Resonant renaming, pass 3: an organ is grafted and shed

The last two neutral nouns in the command surface. `loadmod` / `unloadmod` were the verbs of a
module system; the layer holds organs now, and an organ is **grafted** and **shed**. Internals
follow (`cmd_graft`, `cmd_shed`), as do the usage strings and the `help` listing.

### Verification
- Baseline re-frozen first, this time with the scenario actually calling both commands —
  including a failing `shed nosuchorgan`, so the error path is covered: md5
  `c9b718a6279aac55260c5d80e538e814`, three cold runs identical.
- The diff against it is the rename and nothing else: filtering lines that mention
  `loadmod|unloadmod|graft|shed` leaves **0**.
- `grep -c` for the old nouns over `amosoz.c` returns **0**. `make` 0 errors; selftest
  **60/60**, 0 FAIL; shell treaty **ALL PASSED**; `html_selftest` **43/43**; ASan **0**.

---

## 2026-08-10 — Resonant renaming, pass 2: the OZ layer takes the Method's own words

Pass 1 gave the citizens their name (monads). The extension layer around them still spoke in
neutral engineering nouns. Three concepts move; two deliberately do not.

- **`Module` → `Organ`.** The word the Method actually uses everywhere else — an organ is
  measured, grafted, rejected. `organ_register`, `organ_unload`, `organ_validate_contracts`,
  `MAX_ORGANS`, command `organs`.
- **`Hook` → `Pulse`**, and **`fire_hook` → `galvanize`** (Oleg's pick, out of the TRIPD
  archive's vocabulary). A hook is "called"; a pulse travels and things answer it.
  `pulse_find`, `pulse_init`, `MAX_PULSE_LISTENERS`, `listeners` instead of `subscribers`,
  command `pulses`.
- **`OZLedger` → `Wake`.** The trace a passage leaves behind. The project's own motto — *every
  command leaves a trace* — stops being a metaphor. `wake_record`, `WakeEntry`, `MAX_WAKE`;
  `trace` stays as the verb that reads it.
- **`Slot` stays**, because it is already the canon's word (`AM_SpawnSlot`, `AM_ChannelSlot` in
  AML, slots in SARTRE) and the code's own line says what it means: *a slot is a promise with a
  boundary*. **`contracts` stays** — "OZ begins where extension becomes accountable" already
  names it correctly. Renaming those would be drifting from the canon for the sake of prettiness.
- Also renamed where the concepts live in **data**, not only in identifiers: the slot names
  `ai.hooks` → `ai.pulses`, the built-in organ `oz_ledger` → `oz_wake` and the commands it
  provides (`ledger_size` → `wake_size`), the contract `oz.ledger` → `oz.wake`. Leaving those
  stale is exactly the half-rename that `sigmask` taught us to avoid.
- **Not renamed, and flagged rather than slipped in:** `loadmod` / `unloadmod` still carry the
  old noun. `graft` / `shed` would fit the organ register, but that was not in the three agreed
  and scope expansion is a fork to be agreed, not taken.

### Verification
- **Pass 1 (internals only) is byte-identical.** A 45-command scenario was frozen first —
  244 lines, md5 `6d7c948458adb1e1abc340a43b26e9b4`, reproducible across three cold runs — and
  the ledger→wake and module→organ passes reproduce it exactly. Two visible strings my first
  substitution touched by accident were reverted to keep the proof clean, then changed
  deliberately in pass 2. One declared exception: the selftest label `hook_boot_fired` →
  `pulse_boot_fired`, which is the check naming the renamed field.
- **Pass 2's diff is the rename and nothing else.** Every differing line against the frozen
  baseline contains one of the six words; filtering those out leaves **0 lines**. Re-frozen at
  md5 `a77ed43a40459b58251dac115e55c042`, reproducible across three cold runs.
- No stale word survives: `grep -c` for `module|Module|hook|Hook|ledger|Ledger` over `amosoz.c`
  returns **0**, and over `README.md` **0**.
- `make` 0 errors; selftest **60/60**, 0 FAIL; shell treaty **ALL PASSED**; `html_selftest`
  **43/43**; ASan **0** on the scenario.

---

## 2026-08-07 — A monad can look at a channel before it reads one

The previous entry shipped talking monads with a hazard stated but unguarded: a monad reading an
empty channel froze the whole kernel while `am_channel_read` polled. The canon now has the two
calls a scheduling host needs (`am_channel_try_read`, `am_channel_depth`, and the `CHANNEL TRY` /
`CHANNEL DEPTH` directives — `ariannamethod.ai` `ded407c`), re-vendored here.

- **`channel`** reports the number of active channels; **`channel <name>`** reports its depth,
  and refuses a name that does not exist rather than answering 0 — the difference between "empty"
  and "absent" is exactly what a monad's author needs to see.
- The shipped reader `runtime/talk_read.aml` now uses `CHANNEL TRY`. Same conversation, same
  value crossing (`tension 0.688`), no stall.

**Measured, same reader, empty bus:** `CHANNEL READ` **1.32 s** wall versus `CHANNEL TRY`
**0.02 s** — sixty times.

### Verification
- `channel bus` reads `1 queued` after the writer and `0 queued` after the reader took the value;
  `channel nosuch` refuses; the two monads still talk.
- The stall guard runs the probe three times and requires under 2 s. Falsified: switching the
  probe to `CHANNEL READ` fails it with `4s for 3 runs`.
- **The guard was empty on its first two attempts, and falsification caught both.** The first
  timed the shipped reader — but each treaty case is a fresh process where the channel was never
  created, and `am_channel_read` returns *immediately* on a **missing** channel; only an existing
  empty one polls. So the probe now creates the channel itself. This is the fifth empty test in
  this arc, all the same shape: an assertion that cannot distinguish the two cases it exists to
  separate. A test that has never been run against a deliberately broken build is decoration.
- Selftest **60/60**; shell treaty **ALL PASSED**; `html_selftest` **43/43**; ASan **0** on the
  channel paths.

---

## 2026-08-02 — Two monads talk: AML channels cross the boundary between citizens

Until now a monad could only shout into the shared field or drop text in a mailbox. The language
has had channels all along (`CHANNEL CREATE / WRITE / READ`, spec §20) — they simply did not
deliver, because `CHANNEL READ` bound the value into a scope top-level code never reads
(fixed in the canon, `ariannamethod.ai` `d30995e`, re-vendored here).

`runtime/talk_write.aml` puts `0.7` on a bus; `runtime/talk_read.aml`, a **separate monad**
scheduled later, reads it and sets `TENSION` from it. The field carries `0.694` — the value
crossed from one citizen to another through the language's own primitive, not through anything
amosOZ invented.

### Verification
- Cross-repo falsification: amosOZ rebuilt against the **pre-fix** canon (`d30995e~1`) fails the
  new treaty case with `the value did not cross between monads`; against the fixed canon the
  suite passes. The test proves the canon fix actually reaches a scheduled monad rather than
  merely sitting in the vendored copy.
- Re-vendor is 8 lines, nothing else drifted. Selftest **60/60** cold and mid-live-system;
  shell treaty **ALL PASSED**; `html_selftest` **43/43**; ASan **0** on the channel path.

**The hazard, measured rather than guessed.** A monad reading an *empty* channel stalls the whole
single-threaded kernel: `am_channel_read` polls 1000 × 1 ms before giving up. Same program, same
monad — **1.27 s** wall clock on an empty bus versus **0.01 s** on a full one. One badly ordered
`.aml` freezes the system for over a second per read.

Nothing in amosOZ can currently prevent it, and neither can the program itself: the canon exposes
only `create / write / read / count / close_all` — there is **no** depth query and **no**
non-blocking read (0 matches in the header). A scheduling host needs both. That is canon work,
on its own branch, by the maintainer's word; until then the ordering — writer before reader — is
the only guard, and it is a convention, not a mechanism.

---

## 2026-08-02 — The selftest told the truth only at boot; now it holds in a live system

The two defects named in the previous entry and deliberately left there are fixed, because a
diagnostic that reports 60/60 on a cold boot and 58/60 in a running system is a diagnostic you
cannot use to check a running system.

- **`slice_preempt`** spawned a monad with `max_slice 2` and expected preemption after two ticks.
  In a busy system that monad may be served only once, so the slice never exhausts and the check
  fails on the luck of scheduling. It now runs its probe at `priority 9`, the same isolation the
  mood cohort uses: preemption is measured on the monad under test.
- **`current_has_state`** matched `pid == curp` **without checking `used`**. A freed slot keeps
  its old pid, so a stale slot answered the match and the check failed on a dead monad. Worse,
  the `CHECK` sat *inside* the loop — it ran once per match, so a live system with several stale
  slots moved the reported test total itself.

Fixing the match then exposed something worth stating: the block above frees a slot by hand
without a tick, so if that monad was current, `current_pid` names a freed slot until the
scheduler runs again. **The kernel is not at fault** — verified directly: after `wait` reaps a
running monad, `current` names init, because the tick's never-none guarantee re-establishes it.
The check is now taken after a tick, where the kernel actually holds the invariant.

- **`cmd_tick` reported `running=idle`** whenever the served monad exhausted its slice in the
  same tick and was set back to ready. It scanned for state `"running"`; it now names the monad
  at `K.current_pid`, which the kernel guarantees. The scheduler had always chosen — the display
  could not see it.

### Verification
- **60/60, 0 FAIL in all three contexts**: cold boot, a warm `.soma`, and `selftest` invoked
  mid-session inside the running 41-command scenario. The total is now the same number in every
  context, which it was not before.
- Baseline re-frozen at md5 `6585db04d1ee197ddeeafaeb2a7e1bc7`, reproducible across three cold
  runs; 16 lines differ from the previous baseline — the selftest line, the two former failures,
  and the tick lines that no longer say `idle`.
- `make` 0 errors; shell treaty **ALL PASSED**; `html_selftest` **43/43**; ASan **0** errors.

---

## 2026-08-02 — `mood_bends_scheduler` owned one input and thought that was determinism

Found by running the frozen rename scenario against the merged tree: the check passed on a cold
boot and **failed** when the field came from a persisted `.soma`. The earlier "determinism fix"
set `emergence` and verified five cold runs — but five runs of the *same* cold field prove
nothing about a different field. Identical repetitions of one configuration are not evidence of
independence from configuration.

Two inputs were unowned, and both are now pinned by the check itself:
- **The field.** A warm `.soma` carries dissonance and shared tension/pain, so the pressure and
  shared-chamber terms in `monad_choose` swamped the moods the check was measuring. It now zeroes
  every field term it does not want and raises only `emergence`.
- **The monad table.** Other citizens with their own moods skew the cohort. The check now runs
  its four monads at `priority 9`; priority dominates the score (×1000), so the scheduler is
  choosing *between the cohort members* and the mood is the only difference left.

### Verification
- Passes in three configurations, not one repeated: cold `.soma`, a `.soma` left by the 41-command
  scenario, and `selftest` invoked **mid-session inside a live system**.
- The scenario's own selftest is back to **58/60** — the same two failures the frozen baseline
  always carried, with `mood_bends_scheduler` now passing *because it is isolated* rather than by
  luck.
- Baseline re-frozen at md5 `a8fe8b4d43bcb646c75fbe0ee0d1c46a`, reproducible across three cold
  runs. It differs from the pre-rename baseline in 10 lines, all **after** the selftest: the
  cohort at priority 9 consumes ticks that previously went elsewhere, so the tick counter and
  `cpu_time` totals land differently. The rename's own byte-for-byte proof stands as recorded —
  this change came after it and is declared, not folded into it.

**Two pre-existing defects found and deliberately left alone** (not mine, named rather than
silently fixed):
- `slice_preempt` and `current_has_state` fail whenever `selftest` runs in a live system. They
  assume a nearly empty monad table and have always been boot-only; the frozen baseline shows
  both failing at 58/60 long before this pass.
- `cmd_tick` reports `running=idle` whenever the served monad exhausted its slice in the same
  tick, because it scans for state `"running"` (`amosoz.c:2507`) instead of naming
  `K.current_pid`, which the kernel guarantees is always set. A display bug, not a scheduler one.

---

## 2026-08-02 — Resonant renaming, pass 1: the kernel calls its citizens monads

The roadmap's open item, done as one deliberate pass over **one concept** rather than a sweep
over 193 static functions. The code already called them monads in its own comments (`spawned=1`
real, `0` = virtual monad, the SARTRE ns contract); the identifiers said `Process`. Now they
agree: `Monad` / `MonadTable` / `MAX_MONADS`, `K.monads`, and the lifecycle verbs —
`monad_spawn`, `monad_kill`, `monad_collect` (was `proc_wait`), `monad_collect_any`,
`monad_become` (was `proc_exec` — exec replaces the image), `monad_tick`, `monad_choose` (was
`proc_field_select` — it is where the parliament decides), `monad_is_orphan`,
`monad_spawn_real`, `monad_reap_real`, `monad_open_program`, `monad_run_slice`,
`monad_release_program`.

**What was deliberately left alone.** The Unix treaty userland is untouched — `ls`, `ps`,
`kill`, `cd`, pipes, redirects are the treaty, and renaming them would be renaming the point of
the project. The `/proc` helpers keep their `proc_` prefix (`proc_is_virtual`,
`proc_refresh_all`, `proc_write_file`): they serve the `/proc` surface, not the process concept.
And the sweep stopped at the monad concept — `fs_find` gains nothing by becoming resonant, and a
rename that touches everything is a rename nobody can review.

### Verification
- **A rename must be behaviourally empty, so the invariant is the output itself.** A 41-command
  scenario (boot, spawn, moods, ticks, signals, fs, pipes, memory, devices, OZ meta, selftest,
  field, an `.aml` monad, wait) was frozen **before** the change: 312 lines,
  md5 `1e449ddd9ebf80aac82fa07e11a2c093`, verified reproducible across three cold runs.
  After the rename the same scenario produces **the same md5** — byte for byte, twice: once
  after the type/function pass and again after the `procs` → `monads` field pass.
- Not half-applied: `grep` for every old name (`Process`, `ProcessTable`, `MAX_PROCS`, all 14
  `proc_*` lifecycle verbs, the `procs` field) returns **0**. A half-rename is the `sigmask`
  failure mode and is worse than none.
- `make` 0 errors; selftest **60/60**, 0 FAIL; shell treaty **ALL PASSED**; `html_selftest`
  **43/43**; ASan **0** errors on the same scenario.

---

## 2026-08-01 — Brick D3: a monad's life is its program, not one command

D1 gave amosOZ an AML citizen that ran to completion inside the command that spawned it — a
citizen with a lifespan of one prompt. The canon now has resumable execution
(`am_program_open` / `step` / `close`, `ariannamethod.ai` `ed7347e`), re-vendored here, so a
monad is opened and left **ready**: the scheduler hands it a quantum like any other proc and it
runs `max_slice` statements of its own program per turn. `slice <pid> N` therefore tunes how
finely a monad is sliced — an existing knob, no new one.

- Each slice charges `cpu_time` by the statements actually consumed (`am_program_remaining`
  before/after), and folds what the program moved in the shared field into the monad's own
  mood. A program that keeps perturbing keeps its argument alive against the per-tick fade —
  this is what D2's slice was built for and could not previously be fed.
- `proc_release_aml` closes the program wherever a slot is freed (parent `wait`, init reap).
  A monad killed mid-program becomes a zombie holding its program until it is collected.
- Programs are read into a 64 KB buffer; larger is refused with a clear message rather than
  silently truncated.

**Foundation change, stated loudly rather than slipped in: init now reaps only orphans.**
`proc_tick` freed *every* zombie whenever init was scheduled, so a parent's `wait` was racing
init for its own child's exit code — D1 papered over this by suppressing one auto-tick, which
stops working once the monad dies inside a later tick instead of inside its own command. A
zombie whose parent is still alive now waits for that parent (`proc_is_orphan`); `ppid <= 1` or
a vanished parent still goes to init. This is the correct Unix rule and it makes `wait`
meaningful for real children too, not just for monads.

### Verification
- **Slicing is real, measured on the field:** `run runtime/pulse.aml` with `slice 3 1` leaves the
  monad `ready` after its first quantum with `dissonance 0.699 tension 0.399 pain 0.000` — the
  third directive has not run yet; `pain 0.200` arrives on the next quantum. A partial field is
  the proof that the program is genuinely cut into slices rather than deferred whole.
- `wait` → `reaped PID 3 status 0`, the parent collecting its own monad.
- Selftest **59 → 60**, 0 FAIL. `aml_monad_runs` is replaced by two stricter checks:
  `aml_monad_opens_alive` (opened, holding a program, field *not yet moved*) and
  `aml_monad_finishes` (runs across ticks at `max_slice 1`, ends a zombie with rc 0, field moved).
- Two existing cases were updated deliberately, and both got **stronger**, not looser:
  the numbness case now asserts the delivered victim is `zombie` (absence would also be satisfied
  by a monad that never ran); the mood case asserts both moved dimensions are non-zero after the
  program has run, because the exact value now legitimately depends on scheduling — measured
  `pain 0.162 tension 0.324` at `max_slice 5` and `pain 0.180 tension 0.248` at `max_slice 1`.
- `make` 0 errors; shell treaty **ALL PASSED**; `html_selftest` **43/43**; ASan **0** errors on
  the selftest and on the run-aml / slice / mid-program kill / wait / missing-file paths. Leak
  detection is unsupported on this host, so leaks are not claimed clean by tool; every open is
  paired with a close through `proc_release_aml`.
- README not touched in this commit: it is being rewritten in parallel. Its `.aml` section still
  describes the D1 promise (a monad that completes inside its command) and needs the D3 text.

---

## 2026-07-28 — Brick D2: the monad's own weather bends the scheduler

The field was one weather for the whole system: a monad could push the barometer but had no
state of its own, so two monads with opposite dynamics simply overwrote each other. D2 gives
each monad a thin slice — `mood_pain / mood_tension / mood_flow / mood_warmth` — and lets the
scheduler hear it. Velocity is deliberately **not** in the slice: tempo belongs to the system,
not to one monad.

- **The argument.** `proc_field_select` adds `w_chamber * ((tension+pain) - (flow+warmth)) * 50`
  per proc: a monad's own agitation demands the CPU, its own flow/warmth is content to yield.
  The weight is the shared emergence — the weather decides how loudly anyone may argue. An
  empty mood contributes exactly 0, so a calm system reduces to the previous scheduling bit
  for bit. The pre-existing shared-field chamber term is untouched.
- **`mood <pid>`** reads the slice, `mood <pid> <pain|tension|flow|warmth> <value>` sets one
  dimension. `fork` clones the mood — a child inherits it.
- **An AML monad records its own delta.** What `run x.aml` moves in the shared field is also
  what that monad now carries: `pulse.aml` (PAIN 0.2 / TENSION 0.4) leaves `mood 3` reading
  `pain 0.200 tension 0.400`. The weather keeps the change; the monad keeps its share.
- **Two anti-jam rules, both found by the gate, not by reading.** First cut: a tense monad won
  every round forever and its rival got *exactly 0* CPU — so a served monad now discharges its
  mood (×0.85). That alone did not fix it, because the discharge only reaches whoever is being
  served: the rival's contentment never decayed and kept it out permanently. So a mood now also
  fades for every monad each tick (×0.90) — it is passing weather, not a caste.

### Verification
- Measured, 3 cohorts: with `tension 0.9` vs `warmth 0.9` over 12 ticks → **26 vs 0**; over 60
  ticks → **82 vs 31** (loud, but no starvation); with no moods over 12 ticks → **6 vs 6**, the
  round-robin exactly as before the slice existed.
- Selftest **58 → 59**, 0 FAIL. `mood_bends_scheduler` asserts both halves: divergence with
  moods *and* a zero gap without them.
- **The check flapped and was fixed, not accepted.** It passed with a persisted `.soma` and
  failed from a cold boot, because the mood is weighed by emergence and `am_step` has not run
  during the first command. It now sets its own emergence and restores the field: 5 cold runs
  give an identical 59/59 list, and a warm run matches them.
- Same class in the shell suite: `amos.soma` leaked between cases, so the `.aml` case perturbed
  the field and bent scheduling in a later case (the control read 13 vs 0 instead of 6 vs 6).
  `run()` now clears the field per case — the suite passes twice in a row, order-independent.
- `make` 0 errors; shell treaty **ALL PASSED**; `html_selftest` **43/43**; ASan **0** errors on
  the selftest and the mood/tick/fork paths.
- Noted, not touched (pre-existing): `/proc/<pid>/status` is one command behind, because
  `fs_read_to_buf` refreshes only after *finding* the node (`amosoz.c:530`), and the node is
  created by the previous refresh. A just-spawned proc is therefore invisible in `/proc`;
  `mood <pid>` is the observable for a monad that dies inside its own command.

---

## 2026-07-28 — Brick D1: an AML program is a monad (`run <x.aml>` executes)

`run` on kind `aml` answered "runtime wired later" since brick #2. It runs now. An AML program
is a monad's whole life: it is spawned, it executes, it is charged for the work it did, and it
is a zombie the moment it finishes — so `wait` reaps it exactly like a real child.

- `run <path>.aml` and manifest slots of kind `aml` both go through `proc_aml_spawn` →
  `am_exec_file`. A named `.aml` target is always an AML claim: unreadable is an **error**, not
  a silent fallback to a virtual monad (the first cut got this wrong and spawned a phantom).
- **Charge:** `cpu_time += ` non-empty lines of the program. A line of AML is a unit of work.
  Deterministic — no wall clock, so the accounting is reproducible like the rest of the tick.
- **Parenthood:** the monad's parent is `shell_pid`, not `current_pid`. Parenting to the drifting
  `current_pid` is exactly what made `wait` miss real children (fixed 07-26 from the other end);
  the first cut here reproduced that bug and `wait` reported "no zombie children".
- The auto-tick is held after an AML run (`suppress_next_auto_tick`, the mechanism `fg` uses),
  so the parent — not init — gets the chance to collect the monad.

**Synchronous on purpose, and the reason is measured, not stylistic.** The field is
`static AM_State G` (`aml/ariannamethod.c:91`) with **0** mutexes covering it — the only two
locks in the file guard spawn slots and channels. `am_spawn_thread_fn` calls `am_exec` on a
pthread (`:2824-2827`), and `am_step` touches `G` on 28 lines, so a threaded monad would race
the main loop by construction. Budgeted execution is the right answer to a long program, but
`am_exec_step` / `am_resume` do not exist (**0** matches in the vendored AML). Both roads lead
to the canonical `ariannamethod.ai` — 499 `G.` references across 41 functions, 131 public
`am_*`, 19 repos on that canon — so they travel on their own branch, by the maintainer's word.
Known limitation, stated rather than hidden: a long AML program consumes the quantum whole.

### Verification
- Field moves: `dissonance` 0.000 → **0.700** after `run runtime/pulse.aml`; `ps` shows the
  monad charged `CpuTime 3` for a 3-line program; `wait` → `reaped PID 3 status 0`.
- Manifest path: `run pulse` (kind `aml`, `runtime/slots.tsv`) moves the field identically.
- Refusal: `run runtime/nope.aml` → `cannot read AML program`, and **no** phantom row in `ps`.
- Selftest **57 → 58**, 0 FAIL (`aml_monad_runs`). The check writes its own `.aml` to `/tmp`,
  so it does not depend on cwd, and snapshots/restores the field: `selftest` then `field`
  reports `dissonance 0.002`, identical to a run without the selftest.
- shell treaty **ALL PASSED** with four new cases (monad runs, parent reaps, field moved,
  missing file refused without a phantom); `html_selftest` **43/43**; `make` 0 errors;
  ASan **0** errors on selftest, run-aml, slot and missing-file paths.

---

## 2026-07-27 — The 07-20 CodeQL fix was incomplete: `js/bad-tag-filter` reopened

The 07-20 entry below says three CodeQL alerts were sealed. Code scanning disagrees: that
rule closed one alert and immediately opened a new one at the same line — `tests/html_selftest.mjs:15`.
`<\/script\s*>` still under-matches, because an end tag closes the script even when it carries
trailing garbage: `</script\t\n bar>` is a close. Content could hide past such a tag and the
harness would never see it.

- Pattern is now `/<script\b[^>]*>([\s\S]*?)<\/script\b[^>]*>/i`. The `\b` keeps it honest in
  the other direction — `</scriptfoo>` is not an end tag and must not match.

### Verification
- Four cases through node, old vs new: `</script>` ✓/✓, `</SCRIPT >` ✓/✓,
  `</script\t\n bar>` **✗ old / ✓ new** (the reopened alert), `</scriptfoo>` ✗/✗ (correctly refused).
- `node tests/html_selftest.mjs` → **43/43 PASSED**; the regex still extracts the kernel.

---

## 2026-07-27 — Numbness: the signal mask was unreachable, and it did not survive delivery

`Process.sigmask` was checked in the delivery path (`signals & ~sigmask`), cloned by `fork`,
printed in `/proc/<pid>/status` — and **never set by anything**. Every write in the file was a
zero. The mask was a feature the README claimed (`signals + sigmask`, "masks respected") and
the kernel could not perform. Two things were missing, and either one alone would have left it
dead:

- **No way in.** `numb <pid> <sig>` / `feel <pid> <sig>` — the mask in this system's own
  register rather than a POSIX transliteration. A numbed signal is not dropped: it stays
  pending until the monad feels again, and the next tick delivers it. `numb <pid> 9|19` is
  refused — KILL and STOP pierce any numbness.
- **No persistence.** `proc_tick` ended with `if (!signals) sigmask = 0;`. Numbness is a
  property of the monad, not of the signal being delivered, so the line is gone. Note the
  trigger is *not* an idle tick — the wipe sat inside `if (signals)`, so it fired when an
  unrelated signal was delivered and cleared: a proc numb to TERM that received a CONT lost
  its numbness silently, and the next TERM killed it.

The field is now `Process.numbness`; `/proc/<pid>/status` reports `Numbness:`. Naming follows
the SARTRE register the roadmap points at (`sartre_kernel.h` on `yent-inference` `origin/main`:
tongue / overlay / namespace / monad) — `numb` and `feel` are new words in that register, not
borrowed ones. Side effect the roadmap predicted: these names do not collide with any Unix
command, which is what made this area unauditable by vocabulary.

### Verification
- Selftest **53 → 57**, 0 FAIL: `numbness_outlives_delivery`, `numb_blocks_delivery`,
  `feel_delivers_pending`, `kill_pierces_numbness`.
- **Falsification, not assertion:** re-inserting the wipe line into a scratch build turns the
  suite red — C selftest `[FAIL] numbness_outlives_delivery` + `[FAIL] numb_blocks_delivery`
  (2 TESTS FAILED), shell treaty `FAIL: numbness did not outlive an unrelated delivery`. The
  first version of the C check did *not* fail that build; it was rewritten until it did.
- End-to-end: `run victim; numb 3 15; signal 3 18; tick; signal 3 15; tick; tick; ps` keeps
  victim in the table; the same input against the wipe build loses it. `feel 3 15` then
  delivers the pending TERM on the next tick.
- `grep -c sigmask amosoz.c` → **0** (rename is complete, not half-applied).
- Regression: `make` 0 errors; shell treaty **ALL PASSED**; `html_selftest` **43/43**;
  ASan clean on the selftest and numbness paths.

---

## 2026-07-27 — CRITICAL: stack overflow on a 9-stage pipeline (`shell_execute_line`)

A plain shell line crashed the kernel. `shell_execute_line` splits on `|` into
`pipe_parts[8][MAX_CMD_LEN]`; the splitting loop stops at `nparts < 8`, but the **tail**
segment was written afterwards with no bound at all. A pipeline of 9 segments therefore
wrote 1023 bytes into `pipe_parts[8]` — one full row past the array, straight into the
stack frame. Present since `036d16a` (the v0.3.0 treaty shell), and missed by the earlier
ASan pass and the Codex audit because neither drove a pipeline past the cap.

- `MAX_PIPE_PARTS` replaces the literal `8` in the declaration and the loop — the drift
  between those two literals and the unguarded tail is what the bug was made of.
- The tail write now takes the same bound. A pipeline that does not fit is **refused**
  (`amosoz: too many pipeline stages (max 8)`) rather than silently losing its stages.
- Both writes moved `strncpy` → `snprintf`, which also terminates a segment of exactly
  `MAX_CMD_LEN - 1` bytes (the old `strncpy` left that case unterminated).

### Verification
- Repro before the fix: `echo PIPE9 | cat ×8` → ASan `stack-buffer-overflow, WRITE of size
  1023 at amosoz.c:3783`; the plain build aborts with **rc 134**.
- After: the same input exits **rc 0** and prints the refusal; ASan errors **0**.
- Boundary intact: `echo PIPE8 | cat ×7` (exactly 8 stages) still pipes through, ASan clean.
- A segment of exactly 1023 bytes: ASan clean.
- Both cases are now locked in `tests/shell_treaty.sh` — the new suite run against a build
  of the pre-fix `amosoz.c` dies with **rc 134**, so the tests genuinely fail on the old code.
- Regression: `make` 0 errors; C selftest **53/53**, 0 FAIL; shell treaty **ALL PASSED**;
  `html_selftest` **43/43**; ASan clean on the selftest path.

---

## 2026-07-21 — Brick #3 (commit 1): the AML resonance field — hosted, evolving, persisted

Brick #3 dissolves the discrete tick into a continuous field. Commit 1 brings the **real** field
engine into amosOZ (the standalone superset hosts the field its SARTRE cuts carry). No hand-rolled
affect vector — the actual AML field, vendored — so there is no throwaway intermediate to rebuild.

- **Vendored** the AML core into `aml/ariannamethod.{c,h}` (upstream `842ad91`, freshly pulled).
  The heart stays one file (`amosoz.c`); the field engine lives in a folder. Makefile builds
  `amosoz.c aml/ariannamethod.c -lm` — the field physics need only libm (BLAS/CUDA optional).
- `am_init()` boots the field at kernel init; `am_exec("SCHUMANN 7.83 / PROPHECY 3 / DESTINY 0.3 /
  LAW DEBT_DECAY 0.998")` sets defaults; `am_field_load("amos.soma")` restores a persisted field
  over the defaults when present.
- **The main-loop step is now a field step:** each command advances the field via `am_step(0.1)`
  — schumann phase advances, prophecy debt decays, the LAWS OF NATURE are enforced. The field
  genuinely evolves; it is not inert-alongside the tick.
- `field` command reads `am_get_state()` (schumann / resonance / entropy / emergence / pain /
  tension / dissonance / debt / destiny / prophecy / wormhole / velocity / scars).
- `am_field_save("amos.soma")` on exit; `.soma` gitignored (runtime state). The field remembers
  across runs.

### Verification
- `make` (`-Wall -Wextra`): rc 0, zero amosoz.c errors, vendored AML compiles clean (0 warnings).
- C selftest **50/50**; shell treaty **ALL PASSED** (the per-command field step breaks nothing).
- Field evolves: `field` → 5 commands → `field` shows `schumann_phase` 0.000 → 4.386.
- Persistence: run 1 (fresh) starts phase 0.000; run 2 restores from `.soma` and starts mid-cycle
  (phase 3.556). ASan clean on the boot/field/step/persist path.

## 2026-07-26 — Codex audit pass: 2 findings fixed (load hardening + wait/real-child)

An independent Codex audit (adversarial, read-only) of the session's work confirmed the 5 earlier
memory-safety fixes are closed and `proc_field_select` is clean (no div-by-zero — cpu_limit
guarded; indexing within MAX_PROCS; the never-none cascade still guarantees a pick). It surfaced
two real issues, both reproduced with tools here and fixed:

- **HIGH — `cmd_load` left loaded strings non-terminated.** The earlier fix clamped `node_count`/
  `env_count`/`cwd` but not the per-node string fields or env key/value. A crafted `.img` whose
  fields are non-terminated made a later `env`/`ls`/`cat`/`stat` read past a field boundary
  (bounded within the global `K` in this layout, so no ASan crash, but an over-read / adjacent-
  memory disclosure). Fix: after load, force-terminate every fixed-size string in all `MAX_FILES`
  nodes (path/symlink_target/content/perms/owner) and all `MAX_ENV` env entries (key/value).
  Verified: with an all-`0xFF` crafted image, the longest `0xFF` run in `env` output is now exactly
  127 bytes = the `value` field size (was an unbounded over-read); ASan clean.

- **MEDIUM — `wait` did not observe completed real children.** `cmd_wait`'s poll loop only ran
  `proc_tick`/`proc_wait`, never the real-child reaper, and used the drifting `current_pid` as the
  parent. Root cause (found by instrumenting): a real child spawned at the prompt has
  `ppid = shell_pid (2)`, but by wait-time the scheduler had drifted `current_pid` to `1`, so
  `proc_wait(current_pid)` never matched. Fix: call `proc_reap_real()` inside the wait loop, add
  `proc_wait_any()` (tries both `current_pid` and `shell_pid`), and give a just-spawned real child
  a brief `usleep` to exit. Verified: `run /bin/echo hi; wait` → `reaped PID 3 status 0 after 2
  ticks`; the virtual `fork; kill; wait` path still reaps.

### Verification
- `make` (`-Wall -Wextra`): 0 amosoz.c errors. C selftest **53/53**; shell treaty **ALL PASSED**;
  ASan clean on: crafted-image load + `env`, real spawn + `wait`, `resonate` + `tick`.

---

## 2026-07-26 — Brick #3 (commit 3): field interface + A/B/C couplings proven + regression

Commit 2 dissolved the scheduler into the field but only proved coupling A (velocity)
behaviorally. This commit gives the field a shell handle, proves B and C the same way, and puts
the field arc under the selftest.

- **`resonate <AML directive>`** — the shell perturbs the field via `am_exec` (a command IS a
  field perturbation): `resonate DISSONANCE 0.9`, `resonate VELOCITY RUN`, `resonate TENSION 0.7`.
  This is the handle the scheduler reads through `proc_field_select`.
- **B and C proven behaviorally** (same method as A), scheduling a 3-proc cohort:
  - C (dissonance): calm `y init x z` vs `resonate DISSONANCE 0.95` → `init z x init` — pressure
    surfaces different procs.
  - B (chambers): calm `y init x z` vs `resonate RESONANCE/TENSION/PAIN` (emergence≈0.78) →
    `x z amossh y` — the chamber balance bends switching.
  - Honest scope: the field dimensions are coupled by design (DISSONANCE also lifts
    resonance/emergence in `am_step`), so B/C are not perfectly isolated forcings; A (velocity)
    is the cleanest-isolated proof. The point proven is that all three couplings are **live** and
    bend selection, while a calm field reduces to round-robin.
- **Field selftests** (53/53 now): `field_hosted`, `field_step_advances`, `field_perturb_read`.
  The block snapshots the whole `AM_State` and restores it, so selftest leaves the live field
  untouched (verified: post-selftest field == any single-command field).
- **Measured finding:** the field's natural resting state after one `am_step` is resonance≈0.94,
  emergence≈0.78 (schumann coherence drives resonance up). So the chamber weight (=emergence) is
  live even at rest — but the affect poles (tension/pain/flow/warmth) are 0 at rest, so the
  chamber term is weight×0 = 0, and calm scheduling still reduces to round-robin (confirmed by
  the 53/53 selftest).

### Verification
- `make` (`-Wall -Wextra`): 0 amosoz.c errors. C selftest **53/53**; shell treaty **ALL PASSED**;
  html selftest **43/43** (separate kernel, unaffected); ASan clean on selftest + resonate + tick.

---

## 2026-07-21 — Brick #3 (commit 2): the scheduler dissolves into the field

Commit 1 hosted the field. Commit 2 dissolves the round-robin: the scheduler's primary
selection is now **read from the AML field each step**, not a fixed rule. This is not a choice
between regimes — A (velocity tempo), B (chamber balance), C (dissonance pressure) are all
present every step, and their weight is itself read from the field now (`proc_field_select`):

- **Base:** priority dominates (×1000). The field never overrides a priority level — it governs
  the choice **among equal-priority ready procs**, exactly where the old round-robin was
  arbitrary. Coefficients are tunable to deepen the dissolution further.
- **A (velocity):** NOMOVE/BREATHE bias holding the current proc; RUN biases preemption; WALK is
  neutral — so a **calm default reduces exactly to priority + round-robin**.
- **B (chamber, weight = emergence):** flow+warmth favor continuity (keep current); tension+pain
  favor switching away.
- **C (pressure, weight = dissonance):** surface the cpu-heaviest proc; past the tunnel threshold
  with an active wormhole, jump the round-robin origin (a tunnel skip).
- The field advances deterministically (`am_step`, no RNG in the step path — verified), so the
  coupling law is reproducible. The existing never-none cascade remains the guaranteed floor;
  the slice/cpu-limit/signal housekeeping is untouched.

### Verification
- `make` (`-Wall -Wextra`): 0 amosoz.c errors/warnings (91 warnings all pre-existing).
- **Regression (calm = round-robin):** C selftest **50/50**; shell treaty **ALL PASSED** — the
  calm-field default reproduces the old scheduling, so the dissolution is backward-compatible.
- **Coupling proven behaviorally (A):** booting the field with `VELOCITY NOMOVE` vs `WALK` and
  ticking a 3-proc cohort — NOMOVE holds the current proc (`running = amossh amossh amossh`)
  while WALK rotates (`running = b init a c`). The scheduler genuinely reads the field. (RUN
  coincides with WALK in this scenario because WALK already rotates every tick.) B and C use the
  identical `am_get_state()` read path in the same function; a full A/B/C behavioral matrix is
  the next verification. ASan clean on the scheduler path.

---

## 2026-07-21 — Brick #2: declarative slot manifest (agnostic targets)

Brick #1 made `run <real-path>` spawn a real process. Brick #2 makes `run` **fully agnostic**
through a data-declared manifest, borrowing ariannamethod.cli's `runtime/slots.tsv` shape.

- `runtime/slots.tsv` — a TSV registry: `slot │ label │ kind │ state │ target │ notes`. Loaded
  at boot (`slot_manifest_load`), tolerant of a missing file (standalone use = no slots). Parsing
  is `snprintf`-bounded throughout.
- `run <name>` resolution order: a real host executable (path + `X_OK`) → real process (brick #1);
  else a manifest slot → resolve by `kind` — `baked`/`repo` fork+exec the target as a real
  process, `script`/`aml` are recognized with their runtimes wired later; else any name → a
  virtual monad (unchanged).
- Ships one demo slot: `hello → /bin/echo` (`run hello <text>` prints via a real `/bin/echo`).

### Verification
- `-Wall -Wextra` clean; C selftest **50/50**; shell treaty **ALL PASSED**.
- `run hello MANIFEST_OK` → "Started slot 'hello' -> /bin/echo" and the real child prints
  `MANIFEST_OK`; `run somevirtualname` is still a virtual monad; ASan clean.

---

## 2026-07-21 — Brick #1: real process slots (start of "make processes real")

`run` was a simulation — `run foo` added a bookkeeping row. First brick of the "make it real"
arc: `run <real-executable>` now spawns a **real OS process**.

- `Process` gains `spawned` + `real_pid` — the SARTRE namespace contract (`spawned=1` real,
  `0` = virtual monad).
- `proc_real_spawn` = `fork` + `setrlimit` + `execvp` into a slot; `proc_reap_real` = a
  non-blocking `waitpid` reaper (a dead real child → zombie → freed by the existing paths),
  called each main-loop iteration.
- `run` is **agnostic**: a target that is a real host executable (contains `/` and passes
  `X_OK`) forks+execs a real process; any other name stays a virtual monad (unchanged), so the
  tick-sim and the whole selftest keep working. (`.amos` runs via the shell; `.aml` is a
  recognized kind whose runtime is wired later.)
- `ps` marks a real slot `[real:<hostpid>]`; `kill <pid>` on a real slot sends a real
  SIGTERM → grace → SIGKILL and reaps.
- macOS caveat (measured earlier in the SARTRE work): `RLIMIT_AS` is a no-op on Darwin; the
  memory cap is real on Linux.

### Verification
- `make` (`-Wall -Wextra`): clean, no new warnings on the new code.
- C selftest **50/50**; shell treaty **ALL PASSED** (virtual path untouched).
- `run /bin/echo AMOS_REAL_SLOT_OK` → the string is printed by a **real** child; `ps` shows
  `[real:<pid>]`; no host zombies leaked; ASan clean on the spawn/reap path.

---

## 2026-07-20 — Harden 4 memory-corruption bugs + 1 latent (ASan-driven)

An AddressSanitizer-driven audit of `amosoz.c` found four reachable memory-corruption bugs
(each reproduced under `-fsanitize=address`) plus one latent non-termination. All fixed; the
exact crashing inputs now run clean.

### CRITICAL — `fs_resolve` (the path resolver behind every path command)
- **l.476:** absolute paths were `strcpy(out, path)` into the caller's `char[MAX_PATH]` — a
  path > 511 chars overflowed the stack (ASan: WRITE size 702). Now
  `snprintf(out, MAX_PATH, "%s", path)`, matching the existing relative-path branch.
- **l.492:** `parts[pcount]` was written into a fixed `char[32][64]` with no `pcount < 32`
  guard — a path with >32 components overflowed it (ASan: WRITE size 63). Now guarded and
  each part null-terminated.

### HIGH — `cmd_load` untrusted deserialization (l.2635)
`load` read a raw `VirtualFS` from an untrusted `.img` with no validation; a forged
`node_count` made fs iteration walk past the global `K` (ASan: global-buffer-overflow READ in
`cmd_ls`). Now: `fread` returns are checked (truncated/corrupt → `kernel_init` reset),
`node_count`/`env_count` clamped to their array bounds, `cwd` force-terminated.

### HIGH — `cmd_set` size_t underflow (l.2390)
`strncat(val, argv[i], 127 - strlen(val))` computed the bound as `size_t`; a 127-char word
plus the following unbounded `strcat(val, " ")` pushed `strlen` to 128, underflowing to
`SIZE_MAX` → stack overflow (ASan: WRITE at l.2394). Both appends now use the bounded
`sizeof(val) - strlen(val) - 1` (the safe pattern used elsewhere in the file).

### LATENT — `shell_parse_redirect` (l.3306)
`strncpy(redir_path/redir_in, …, MAX_PATH-1)` could leave the buffer unterminated; the
following `str_trim_inplace` `strlen` would then read OOB (dormant in the current build, but a
real defect). Each `strncpy` now explicitly null-terminates.

### Verification
- `make` (`-Wall -Wextra`): clean, no new warnings on the changed lines.
- C selftest **50/50 — ALL TESTS PASSED**; shell treaty **ALL PASSED** (redirects intact).
- ASan re-run of all four repros (700-char absolute path; >32-component path; forged `.img`;
  `set K <127-char> <…>`): every one now exits 0 with **zero AddressSanitizer errors**.
- Pending: an independent Codex audit pass.

---

## 2026-07-20 — Move reference implementations to `reffs/`

`amosoz.py` and `amosoz.html` are reference forms; `amosoz.c` is canonical. Both moved into
`reffs/` (`git mv`, history preserved) to keep the repo root focused on the C kernel. Test
wiring repointed so every target still resolves:

- `Makefile` (`test-py`): `python3 amosoz.py` → `python3 reffs/amosoz.py`.
- `tests/parity_runner.sh`: same path update.
- `tests/html_selftest.mjs`: `path.join(__dirname, "..", "amosoz.html")` →
  `path.join(__dirname, "..", "reffs", "amosoz.html")`.
- `README.md`: parity references repointed to `reffs/`.

### Verification

- `make` + C selftest: **50/50 — ALL TESTS PASSED**.
- `node tests/html_selftest.mjs`: **43/43 PASSED** — reads `reffs/amosoz.html`.
- `sh tests/shell_treaty.sh`: **ALL PASSED**.
- Python parity (`make test-py` / `make test-parity`) not run this pass; path verified
  statically (`python3 reffs/amosoz.py`, file present).

---

## 2026-07-20 — Fix three CodeQL high-severity alerts

### C — snprintf overflow guard (`amosoz.c`) · alerts #1, #2 (`cpp/overflowing-snprintf`)

Two loops assembled strings with `pos += snprintf(buf + pos, sizeof(buf) - pos, ...)`.
`snprintf` returns the length it *would* have written, so on truncation `pos` can exceed
`sizeof(buf)`; on the next iteration `sizeof(buf) - pos` underflows (evaluated as `size_t`)
to a huge value and `buf + pos` points past the buffer — an out-of-bounds write.

- `/proc/<pid>/fd` listing (`fdlist`): `pos += snprintf(...)` → `pos = safe_append(...)`.
- `nohup` command-line assembly (`cmdline`): both the `pos += snprintf(...)` and the raw
  `cmdline[pos++] = ' '` are routed through `safe_append`; `cmdline[0]` is initialized
  before the loop so the buffer is always a valid C string.

`safe_append` (already defined in the file) is the bounds-checked append: it returns early
when `offset >= bufsz`, calls `vsnprintf` with the correct remaining size, and clamps the
returned offset to `bufsz`. Output for the non-truncating case is identical; only the
overflow path is removed.

### JS — case-insensitive script extraction (`tests/html_selftest.mjs`) · alert #3 (`js/bad-tag-filter`)

The selftest extracted the HTML kernel with `/<script>([\s\S]*?)<\/script>/`, which does not
match an upper-case `<SCRIPT>` tag. Pattern made case-insensitive and tolerant of tag
attributes and trailing whitespace: `/<script\b[^>]*>([\s\S]*?)<\/script\s*>/i`.

### Verification

- `make` (`gcc -Wall -Wextra`): builds, exit 0, no new warnings on the changed lines.
- C selftest (`printf 'selftest\nexit\n' | ./amosoz`): **50/50 — ALL TESTS PASSED**.
- HTML selftest (`node tests/html_selftest.mjs`): **43/43 PASSED** — the new regex still
  extracts the kernel.
- Shell treaty (`sh tests/shell_treaty.sh`): **ALL PASSED**.
- Behavioral: `nohup echo hello world foo` → `hello world foo`; `cat /proc/1/fd` → `(no open fds)`.
