#!/bin/sh
# Triple parity runner — C, Python, HTML selftest
set -e
cd "$(dirname "$0")/.."
FAIL=0

echo "=== C selftest ==="
if printf 'selftest\nexit\n' | ./amosoz | grep -q 'ALL TESTS PASSED'; then
  echo "C: OK"
else
  echo "C: FAIL"
  FAIL=1
fi

echo "=== Python selftest ==="
if printf 'selftest\nexit\n' | python3 reffs/amosoz.py | grep -q 'ALL TESTS PASSED'; then
  echo "Python: OK"
else
  echo "Python: FAIL"
  FAIL=1
fi

echo "=== HTML selftest ==="
if [ -f tests/html_selftest.mjs ]; then
  if node tests/html_selftest.mjs; then
    echo "HTML: OK"
  else
    echo "HTML: FAIL"
    FAIL=1
  fi
else
  echo "HTML: SKIP (tests/html_selftest.mjs missing)"
  FAIL=1
fi

exit $FAIL