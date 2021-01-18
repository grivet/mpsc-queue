# GNU Make

srcdir ?= ..

SRCS = $(sort $(wildcard test/*.c))
OBJS = $(SRCS:%.c=%.o)

TO_BE_CLEANED += $(OBJS)

include $(srcdir)/config.mk

TEST = test/test

test/%.o: test/%.c | Makefile
	$(CC) -pthread -std=c11 -MD -Wall -Wextra $(CFLAGS) -c -I$(srcdir)/src -o $@ $<

$(TEST): $(OBJS) lib/$(LIB).a
	$(CC) -pthread -o $@ $^ $(LDLIBS)

test: $(TEST)

run: test
	$(WRAPPER) $(TEST) -n 1000000 -c 2

-include obj/*.d
-include test/*.d

TO_BE_CLEANED += test/*.d
TO_BE_CLEANED += test/test

.PHONY: all
