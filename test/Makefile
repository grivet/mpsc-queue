# GNU Make

srcdir ?= ..

SRCS = $(sort $(wildcard test/*.c))
OBJS = $(SRCS:%.c=%.o)

TO_BE_CLEANED += $(OBJS)

include $(srcdir)/config.mk

TEST = test/test

test/%.o: test/%.c | lib/$(LIB) Makefile
	$(CC) -pthread -std=c11 -MD -Wall -Wextra $(CFLAGS) -c -I$(srcdir)/src -o $@ $<

$(TEST): $(OBJS)
	$(CC) -pthread -L$(srcdir)/lib -o $@ $^ -lmpsc

test: $(TEST)

run: test
	LD_LIBRARY_PATH=$(srcdir)/lib $(WRAPPER) $(TEST)

-include obj/*.d
-include test/*.d

TO_BE_CLEANED += test/*.d
TO_BE_CLEANED += test/test

.PHONY: all
