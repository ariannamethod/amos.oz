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

# A shell command belongs to the shell, not to whoever the scheduler picked last tick. Before
# the actor/selected split this printed `/`: cd wrote into the selected monad, the auto-tick
# moved the selection, and pwd read someone else's cwd.
out=$(run 'cd /tmp' 'pwd')
echo "$out" | grep -qE '\$ /tmp$' || { echo "FAIL: cd does not survive to the next command"; exit 1; }

out=$(run 'run drifter' 'tick' 'alloc 64' 'cat /proc/2/status')
echo "$out" | grep -q 'for pid 2' || { echo "FAIL: memory charged to the scheduler's pick, not the shell"; exit 1; }

out=$(run 'run drifter' 'tick' 'fork')
echo "$out" | grep -q 'from 2' || { echo "FAIL: fork cloned the scheduler's pick, not the shell"; exit 1; }

# sleep belongs to whoever asked for it. It used to read argv[0] — the command name — so it
# slept 0 ticks, and it blocked the first *running* row, i.e. the scheduler's pick.
out=$(run 'run drifter' 'tick' 'sleep 5' 'ps')
echo "$out" | grep -q 'PID 2 blocked for 5 ticks' || { echo "FAIL: sleep did not block the actor for the asked count"; exit 1; }
echo "$out" | grep -qE '^2[[:space:]]+amossh[[:space:]]+blocked' || { echo "FAIL: the actor is not blocked after sleep"; exit 1; }

out=$(run 'sleep' 'sleep abc')
echo "$out" | grep -q 'Usage: sleep' || { echo "FAIL: sleep without an argument is not refused"; exit 1; }
echo "$out" | grep -q 'must be a positive number' || { echo "FAIL: sleep with a non-number is not refused"; exit 1; }

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

# two monads talking through an AML channel: the writer puts a value on the bus, the reader
# takes it and sets TENSION from it. A zero field would mean the value never crossed.
out=$(run "run $ROOT/runtime/talk_write.aml" 'tick' 'tick' "run $ROOT/runtime/talk_read.aml" 'tick' 'tick' 'tick' 'field')
echo "$out" | grep -q 'AML monad' || { echo "FAIL: talking monads did not open"; exit 1; }
if echo "$out" | grep -qE 'tension 0\.000'; then echo "FAIL: the value did not cross between monads"; exit 1; fi

out=$(run "run $ROOT/runtime/talk_write.aml" 'tick' 'tick' 'channel bus')
echo "$out" | grep -q "channel 'bus': 1 queued" || { echo "FAIL: channel depth does not follow a write"; exit 1; }

# Reading an EXISTING but empty channel is what stalls: am_channel_read polls ~1.3 s, while a
# missing channel returns at once. The probe therefore creates the channel itself, then reads
# it empty — three runs take hundredths with CHANNEL TRY and seconds with CHANNEL READ.
t0=$(date +%s)
for _ in 1 2 3; do run "run $ROOT/runtime/probe_empty.aml" "tick" "tick" >/dev/null; done
t1=$(date +%s)
[ $((t1 - t0)) -lt 2 ] || { echo "FAIL: reading an empty bus stalls the kernel ($((t1-t0))s for 3 runs)"; exit 1; }

# The kernel cannot preempt a slice — control is inside the AML interpreter until the statement
# returns, and a blocking directive there holds the whole system. The quantum is therefore timed
# after the fact: a monad that held it past MAX_SLICE_STALL_MS is counted and stopped.
out=$(run "run $ROOT/runtime/probe_stall.aml" 'slice 3 1' 'tick' 'tick' 'tick' 'tick' 'tick' 'cat /proc/3/status')
echo "$out" | grep -qE 'Stall:[[:space:]]+[0-9]+ ms \(1 over' || { echo "FAIL: a stalling slice was not counted"; exit 1; }
echo "$out" | grep -qE 'State:[[:space:]]+stopped' || { echo "FAIL: a stalling monad kept its turn"; exit 1; }

# and a monad that does not block is untouched by the watchdog
out=$(run "run $ROOT/runtime/probe_empty.aml" 'slice 3 1' 'tick' 'tick' 'tick' 'cat /proc/3/status')
echo "$out" | grep -qE 'Stall:[[:space:]]+[0-9]+ ms \(0 over' || { echo "FAIL: the watchdog flagged a monad that never stalled"; exit 1; }

# An empty run queue is a state, not an emergency. With init and the shell stopped the kernel
# runs IDLE and says so; before the silence locus it named a stopped monad as the current one.
out=$(run 'signal 1 19' 'signal 2 19' 'tick' 'ps' 'current')
echo "$out" | grep -qE '^0 +idle +running' || { echo "FAIL: nobody eligible and IDLE did not take the seat"; exit 1; }
echo "$out" | grep -q 'current pid: 0 (idle)' || { echo "FAIL: the kernel named someone else while idle"; exit 1; }
echo "$out" | grep -qE '^2 +amossh +stopped' || { echo "FAIL: a stopped monad was resurrected to fill the seat"; exit 1; }

# A sleeper is hard state too: the old occupancy guarantee woke it to have someone running,
# which made every sleep shorter than it asked for whenever nobody else was eligible.
out=$(run 'signal 1 19' 'sleep 5' 'ps' 'ps' 'ps')
echo "$out" | grep -qE '^2 +amossh +running' && { echo "FAIL: a sleeping actor was woken to fill the seat"; exit 1; }
echo "$out" | grep -qE '^2 +amossh +blocked' || { echo "FAIL: sleep did not hold the actor blocked"; exit 1; }

# The cpu limit is the kernel's own promise: with every limit spent the system idles rather
# than running someone past it.
out=$(run 'climit 1 2' 'climit 2 2' 'tick' 'tick' 'tick' 'ps')
echo "$out" | grep -qE '^[12] +(init|amossh) +[a-z]+ +[0-9]+ +[3-9]' && { echo "FAIL: a monad ran past its cpu limit"; exit 1; }
echo "$out" | grep -qE '^0 +idle +running' || { echo "FAIL: limits exhausted and the kernel did not idle"; exit 1; }

# IDLE is not a citizen anyone may end, tune or speak for: without it an empty queue has no
# legal answer, and as an actor it would own a cwd, fds and children (`fork` cloned it).
out=$(run 'signal 0 19' 'kill 0' 'climit 0 1' 'nice 0 9' 'slice 0 1' 'limit 0 5' 'mood 0 tension 1.0' 'numb 0 9' 'feel 0 9' 'fg 0' 'fork' 'ps')
echo "$out" | grep -q 'idle takes no signals' || { echo "FAIL: idle accepted a signal"; exit 1; }
echo "$out" | grep -q 'idle cannot be killed' || { echo "FAIL: idle accepted a kill"; exit 1; }
echo "$out" | grep -q 'idle takes no settings' || { echo "FAIL: idle accepted a scheduling knob"; exit 1; }
echo "$out" | grep -q 'idle carries no weather' || { echo "FAIL: idle accepted a mood"; exit 1; }
echo "$out" | grep -q 'idle cannot be brought to the foreground' || { echo "FAIL: idle became the actor"; exit 1; }
echo "$out" | grep -qE '^0 +idle +(ready|running) +[0-9]+ +0 +0 ' || { echo "FAIL: idle left its own state"; exit 1; }
[ "$(echo "$out" | grep -cE '^[0-9]+ +idle ')" = 1 ] || { echo "FAIL: idle was cloned"; exit 1; }

out=$(run 'fortune oz')
echo "$out" | grep -q . || { echo "FAIL: fortune oz"; exit 1; }

out=$(run 'spec')
echo "$out" | grep -q '0.4.0' || { echo "FAIL: spec"; exit 1; }

out=$(run 'doctor')
echo "$out" | grep -q healthy || { echo "FAIL: doctor"; exit 1; }

echo "shell_treaty v0.4: ALL PASSED"