all: run

perf_OBJS := test/perf/ts-mpsc-queue.o
perf_OBJS += test/perf/perf.o
perf_OBJS += test/util.o

CFLAGS := -pthread -std=c11 -MD -Wall -Wextra $(CFLAGS)
CFLAGS += -I$(CURDIR) -I$(CURDIR)/test

test/%.o: test/%.c Makefile
	$(CC) $(CFLAGS) -c -o $@ $<

perf: $(perf_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

.PHONY: run
run: perf
	$(WRAPPER) $(CURDIR)/perf -n 10000000 -c $$(nproc)

-include test/perf/*.d

.PHONY: clean
clean:
	rm -f $(perf_OBJS) $(perf_OBJS:%.o=%.d) perf
