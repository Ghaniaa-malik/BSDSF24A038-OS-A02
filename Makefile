CC = gcc
CPPFLAGS = -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2
LDFLAGS =
LDLIBS =

.PHONY: all clean test
all: bin/ls

bin/ls: obj/ls.o | bin
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

obj/ls.o: src/ls-v1.0.0.c | obj
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

bin obj:
	mkdir -p $@

test: all
	python3 tests/test_ls.py

clean:
	rm -f bin/ls obj/ls.o
