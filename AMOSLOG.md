# AMOSLOG

Reproducible log of changes to amosOZ. Each entry records **what** changed, **why**,
and **how it was verified** — decisions and evidence, not process. Newest first.

## Roadmap (open — not yet done)

- **Resonant renaming.** Move internal function/command names toward the resonant register of
  SARTRE (schumann / overlay / tongue / namespace / prophecy) and AML (destiny / prophecy /
  pain / wormhole / scar), as amosOZ grows its SARTRE-compatible resonance core. The Unix-treaty
  userland (`ls`, `ps`, `kill`, pipes) stays at the surface where it IS the point; the kernel
  internals take the resonant names. This cascades into the command table and touches the treaty
  identity — do it deliberately in one pass, not casually. (Side effect: relaxes vocabulary-based
  classifier pressure on routine OS-code.)

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

Next (commit 2): the scheduler reads dominance from the field; round-robin dissolves.

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
