all: unit perf

CFLAGS_ALL := -pthread -std=c11 -MD -Wall -Wextra $(CFLAGS)
CFLAGS_ALL += -I$(CURDIR) -I$(CURDIR)/test

test/%.o: test/%.c Makefile
	$(CC) $(CFLAGS_ALL) -c -o $@ $<

unit_OBJS := test/unit/unit.o
unit_OBJS += test/util.o

unit: $(unit_OBJS)
	$(CC) $(CFLAGS_ALL) -o $@ $^

perf_OBJS := test/perf/perf.o
perf_OBJS += test/util.o
perf_OBJS += test/perf/ts-mpsc-queue.o

perf: $(perf_OBJS)
	$(CC) $(CFLAGS_ALL) -o $@ $^

.PHONY: run
run: unit perf
	$(WRAPPER) $(CURDIR)/unit \
	&& $(WRAPPER) $(CURDIR)/perf -n 1000000 -c $$(($$(nproc) - 1))

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
