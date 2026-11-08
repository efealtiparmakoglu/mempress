CC      ?= cc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L

SRC = src/mempress.c src/main.c

all: mempress

mempress: $(SRC) src/mempress.h
	$(CC) $(CFLAGS) -o $@ $(SRC)

mempress_tests: tests/mempress_tests.c src/mempress.c src/mempress.h
	$(CC) $(CFLAGS) -o mempress_tests tests/mempress_tests.c src/mempress.c

test: mempress mempress_tests
	./mempress_tests
	./mempress --target 16MB --rate 64MB/s --hold 300ms --report 0 | grep -q 'final: allocated 16.0 MiB'
	./mempress --target 4MB --no-touch --hold 100ms --report 0 --json | grep -q '"allocated_bytes":4194304'
	./mempress 2>&1 | grep -q -- '--target is required'; \
	test $$? -eq 0 && echo "smoke: growth/json/usage-validation OK"

clean:
	rm -f mempress mempress_tests

.PHONY: all test clean
