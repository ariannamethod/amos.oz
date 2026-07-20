# AMOSLOG

Reproducible log of changes to amosOZ. Each entry records **what** changed, **why**,
and **how it was verified** — decisions and evidence, not process. Newest first.

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
