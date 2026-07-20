CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2
LDFLAGS ?= -lm

.PHONY: all run test test-shell test-py test-html test-parity test-all clean

all: amosoz

amosoz: amosoz.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

run: amosoz
	./amosoz

test: amosoz
	printf 'selftest\nexit\n' | ./amosoz

test-shell: amosoz
	sh tests/shell_treaty.sh

test-py:
	printf 'selftest\nexit\n' | python3 reffs/amosoz.py

test-html:
	node tests/html_selftest.mjs

test-parity:
	sh tests/parity_runner.sh

test-all: test test-shell test-py test-html

clean:
	rm -f amosoz amosoz_neo amosoz.img