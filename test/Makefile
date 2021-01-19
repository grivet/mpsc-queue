# GNU Make

srcdir ?= ..

OBJS = $(patsubst test/%.c,test/%.o,$(sort $(wildcard test/*.c)))

TEST = test/test

test/%.o: test/%.c | Makefile
	$(CC) -pthread -std=c11 -MD -Wall -Wextra $(CFLAGS) -c -I$(srcdir)/src -o $@ $<

$(TEST): $(OBJS) lib/$(LIB).a
	$(CC) -pthread -o $@ $^ $(LDLIBS_ALL)

test: $(TEST)

run: test
	$(WRAPPER) $(TEST) -n 1000000 -c 2

-include src/*.d
-include test/*.d

.PHONY: run
