#!/bin/sh
# amosOZ shell + reference command smoke suite (v0.4)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/amosoz"
[ -x "$BIN" ] || { echo "build first: make"; exit 1; }

run() {
  # each case starts from a cold field: amosoz persists the AML field to amos.soma on exit,
  # so without this a case inherits whatever weather the previous one left and the suite
  # becomes order-dependent (the .aml case perturbs tension/pain and bends later scheduling)
  rm -f amos.soma "$ROOT/amos.soma"
  printf '%s\n' "$@" | "$BIN" 2>&1
}

out=$(run 'echo treaty > /tmp/treaty.txt' 'cat /tmp/treaty.txt')
echo "$out" | grep -q treaty || { echo "FAIL: redirect"; exit 1; }

out=$(run 'echo line2 >> /tmp/treaty.txt' 'cat /tmp/treaty.txt')
echo "$out" | grep -q line2 || { echo "FAIL: append"; exit 1; }

out=$(run 'echo piped | cat')
echo "$out" | grep -q piped || { echo "FAIL: pipe"; exit 1; }

out=$(run 'echo PIPE8 | cat | cat | cat | cat | cat | cat | cat')
echo "$out" | grep -q PIPE8 || { echo "FAIL: pipeline at MAX_PIPE_PARTS"; exit 1; }

out=$(run 'echo PIPE9 | cat | cat | cat | cat | cat | cat | cat | cat')
echo "$out" | grep -q 'too many pipeline stages' || { echo "FAIL: pipeline overflow not refused"; exit 1; }

out=$(run 'cat < /etc/hostname')
echo "$out" | grep -q amosoz || { echo "FAIL: stdin redirect"; exit 1; }

out=$(run 'which echo')
echo "$out" | grep -q '/bin/echo' || { echo "FAIL: which"; exit 1; }

out=$(run 'exec /home/user/hello.amos amos')
echo "$out" | grep -q amos || { echo "FAIL: script"; exit 1; }

out=$(run 'echo needle > /tmp/g.txt' 'grep needle /tmp/g.txt')
echo "$out" | grep -q needle || { echo "FAIL: grep"; exit 1; }

out=$(run 'ln -s /etc/motd /tmp/ml' 'cat /tmp/ml')
echo "$out" | grep -q amosOZ || { echo "FAIL: symlink"; exit 1; }

out=$(run 'run victim' 'numb 3 15' 'signal 3 18' 'tick' 'signal 3 15' 'tick' 'tick' 'ps')
echo "$out" | grep -qE '^3[[:space:]]+victim' || { echo "FAIL: numbness did not outlive an unrelated delivery"; exit 1; }

out=$(run 'run victim' 'numb 3 15' 'signal 3 15' 'tick' 'feel 3 15' 'tick' 'ps')
# delivery is now observed directly: init reaps only orphans, so a collected zombie stays
# visible for its parent. Asserting "zombie" is stronger than asserting the row is absent —
# an absent row would also be satisfied by a monad that never ran at all.
echo "$out" | grep -qE '^3[[:space:]]+victim[[:space:]]+zombie' || { echo "FAIL: feel did not deliver the pending signal"; exit 1; }

out=$(run 'run victim' 'numb 3 9')
echo "$out" | grep -q 'pierces numbness' || { echo "FAIL: KILL is maskable"; exit 1; }

out=$(run "run $ROOT/runtime/pulse.aml" 'wait' 'field')
echo "$out" | grep -q 'AML monad' || { echo "FAIL: .aml did not run as a monad"; exit 1; }
echo "$out" | grep -q 'reaped PID' || { echo "FAIL: parent could not reap the AML monad"; exit 1; }
if echo "$out" | grep -q 'dissonance 0.000'; then echo "FAIL: AML monad did not move the field"; exit 1; fi

out=$(run 'run pulse' 'field')
echo "$out" | grep -q 'AML monad' || { echo "FAIL: manifest slot of kind aml did not run"; exit 1; }

# the monad now runs across quanta and its mood accumulates per slice while the per-tick fade
# works against it, so the exact value legitimately depends on scheduling. Assert what does not:
# both dimensions the program moved are non-zero once it has run.
out=$(run "run $ROOT/runtime/pulse.aml" 'tick' 'tick' 'mood 3')
echo "$out" | grep -q 'pid 3 mood' || { echo "FAIL: AML monad has no mood"; exit 1; }
if echo "$out" | grep -qE 'pain 0\.000|tension 0\.000'; then echo "FAIL: AML monad did not record its delta as mood"; exit 1; fi

out=$(run 'run /nonexistent/nope.aml' 'ps')
echo "$out" | grep -q 'cannot read AML program' || { echo "FAIL: missing .aml not refused"; exit 1; }
if echo "$out" | grep -q 'nope.aml.*ready'; then echo "FAIL: missing .aml left a phantom monad"; exit 1; fi

TICKS="tick tick tick tick tick tick tick tick tick tick tick tick"
cpu_of() { echo "$1" | awk -v n="$2" '$2==n{print $5; exit}'; }

out=$(run 'run alpha' 'run beta' 'mood 3 tension 0.9' 'mood 4 warmth 0.9' $TICKS 'ps')
a=$(cpu_of "$out" alpha); b=$(cpu_of "$out" beta)
[ -n "$a" ] && [ -n "$b" ] || { echo "FAIL: mood cohort missing from ps"; exit 1; }
[ "$a" -gt "$b" ] || { echo "FAIL: mood did not bend the scheduler ($a vs $b)"; exit 1; }

out=$(run 'run alpha' 'run beta' $TICKS 'ps')
a=$(cpu_of "$out" alpha); b=$(cpu_of "$out" beta)
[ "$a" = "$b" ] || { echo "FAIL: empty mood no longer reduces to round-robin ($a vs $b)"; exit 1; }

out=$(run 'fortune oz')
echo "$out" | grep -q . || { echo "FAIL: fortune oz"; exit 1; }

out=$(run 'spec')
echo "$out" | grep -q '0.4.0' || { echo "FAIL: spec"; exit 1; }

out=$(run 'doctor')
echo "$out" | grep -q healthy || { echo "FAIL: doctor"; exit 1; }

echo "shell_treaty v0.4: ALL PASSED"