all: unit perf

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
CSTD := gnu11
else
CSTD := c11
CFLAGS += -D_POSIX_C_SOURCE=200809L
endif

CFLAGS_ALL := -std=$(CSTD) -MD -Wall -Wextra -g3 $(CFLAGS)
CFLAGS_ALL += -I$(CURDIR) -I$(CURDIR)/test

test/%.o: test/%.c Makefile
	$(CC) $(CFLAGS_ALL) -c -o $@ $<

test_OBJS := test/util.o
test_OBJS += test/tailq.o
test_OBJS += test/mpsc-queue.o
test_OBJS += test/ts-mpsc-queue.o

unit_OBJS := test/unit/main.o
unit_OBJS += test/unit/mpsc-queue.o
unit_OBJS += $(test_OBJS)

unit: $(unit_OBJS)
	$(CC) $(CFLAGS_ALL) -o $@ $^

perf_OBJS := test/perf/main.o
perf_OBJS += $(test_OBJS)
ifeq ($(UNAME_S),Darwin)
perf_OBJS += test/perf/pthread-barrier.o
endif

perf: $(perf_OBJS)
	$(CC) $(CFLAGS_ALL) -pthread -O3 -o $@ $^

ifeq ($(UNAME_S),Darwin)
NPROC=$(shell sysctl -n hw.logicalcpu)
else
NPROC=$(shell nproc)
endif

.PHONY: test
test: unit perf
	$(WRAPPER) $(CURDIR)/unit &&\
	$(WRAPPER) $(CURDIR)/perf -n 1000000 -c $$(($(NPROC) - 1))

.PHONY: benchmark
benchmark: perf
	$(CURDIR)/test/perf/stats.sh $(CURDIR)/perf 1000000 1
	$(CURDIR)/test/perf/stats.sh $(CURDIR)/perf 1000000 2
	$(CURDIR)/test/perf/stats.sh $(CURDIR)/perf 1000000 4

-include test/perf/*.d
-include test/unit/*.d

.PHONY: clean
clean:
	rm -f unit $(unit_OBJS) $(unit_OBJS:%.o=%.d)
	rm -f perf $(perf_OBJS) $(perf_OBJS:%.o=%.d)
