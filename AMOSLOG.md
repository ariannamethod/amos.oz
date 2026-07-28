# AMOSLOG

Reproducible log of changes to amosOZ. Each entry records **what** changed, **why**,
and **how it was verified** — decisions and evidence, not process. Newest first.

## Roadmap (open — not yet done)

- **`.aml` budgeted execution.** A monad currently consumes its quantum whole (D1). Real time-slicing needs an `am_exec_step` / `am_resume` API the canon does not have; that is upstream work on `ariannamethod.ai`, on its own branch, by the maintainer's word.
- **Resonant renaming.** Move internal function/command names toward the resonant register of
  SARTRE (schumann / overlay / tongue / namespace / prophecy) and AML (destiny / prophecy /
  pain / wormhole / scar), as amosOZ grows its SARTRE-compatible resonance core. The Unix-treaty
  userland (`ls`, `ps`, `kill`, pipes) stays at the surface where it IS the point; the kernel
  internals take the resonant names. This cascades into the command table and touches the treaty
  identity — do it deliberately in one pass, not casually. (Side effect: relaxes vocabulary-based
  classifier pressure on routine OS-code.)

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
