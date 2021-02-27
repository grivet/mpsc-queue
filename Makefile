all: run

CFLAGS := -pthread -std=c11 -MD -Wall -Wextra $(EXTRA_CFLAGS)
CFLAGS += -I$(CURDIR) -I$(CURDIR)/test

test/%.o: test/%.c Makefile
	$(CC) $(CFLAGS) -c -o $@ $<

unit_OBJS := test/unit/unit.o
unit_OBJS += test/util.o

unit: $(unit_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

perf_OBJS := test/perf/perf.o
perf_OBJS += test/util.o
perf_OBJS += test/perf/ts-mpsc-queue.o

perf: $(perf_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

.PHONY: run
run: unit perf
	$(WRAPPER) $(CURDIR)/unit \
	&& $(WRAPPER) $(CURDIR)/perf -n 10000000 -c $$(nproc)

-include test/perf/*.d
-include test/unit/*.d

.PHONY: clean
clean:
	rm -f unit $(unit_OBJS) $(unit_OBJS:%.o=%.d)
	rm -f perf $(perf_OBJS) $(perf_OBJS:%.o=%.d)
