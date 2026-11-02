CC     = cc
CFLAGS = -std=c99 -Wall -Wextra -pedantic -O2
SRC    = src/rizc-cci.c

# build both the normal and the trace-printing emulator
all: rizc-cci rizc-cci-debug

rizc-cci: $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC)

# -DDEBUG turns on per-instruction trace output
rizc-cci-debug: $(SRC)
	$(CC) $(CFLAGS) -DDEBUG -o $@ $(SRC)

# assemble and run the README example (test.rizc-cci)
example: rizc-cci
	python3 assembler.py test.rizc-cci -o instructions.txt
	./rizc-cci test-input.txt instructions.txt output.txt
	cat output.txt

# check every Testcases/1-N case: assembler output, emulator output, debug trace
test: rizc-cci-debug
	@mkdir -p build
	@fail=0; \
	for src in Testcases/*.rizc-cci; do \
		t=$${src%.rizc-cci}; n=$$(basename $$t); \
		python3 assembler.py $$src -o build/$$n-instructions.txt; \
		./rizc-cci-debug $$t-input.txt $$t-instructions.txt build/$$n-output.txt > build/$$n-stdout.txt; \
		if cmp -s build/$$n-instructions.txt $$t-instructions.txt \
		   && cmp -s build/$$n-output.txt $$t-output.txt \
		   && cmp -s build/$$n-stdout.txt $$t-stdout.txt; then \
			echo "PASS $$n"; \
		else \
			echo "FAIL $$n"; fail=1; \
		fi; \
	done; \
	exit $$fail

clean:
	rm -rf rizc-cci rizc-cci-debug build output.txt instructions.txt

.PHONY: all example test clean
