# GNU Make

AR_LIB = lib/libmpsc.a
SO_LIB = lib/libmpsc.so

OBJS := src/mpsc-queue.o

CFLAGS := -std=c11 -MD -Wall -Wextra -Werror $(CFLAGS)

all: $(AR_LIB) $(SO_LIB)

$(SO_LIB): $(OBJS)
	@mkdir -p lib
	$(CC) -shared $(CFLAGS) $^ -o $@ $(LDLIBS)

$(AR_LIB): $(OBJS)
	@mkdir -p lib
	$(AR) -crs $@ $^

src/%.o: src/%.c Makefile
	$(CC) $(CFLAGS) -c -o $@ $<

test_OBJS := $(OBJS)
test_OBJS += src/ts-mpsc-queue.o
test_OBJS += src/util.o
test_OBJS += src/test.o

test: $(test_OBJS) $(AR_LIB)
	$(CC) -pthread -std=c11 -MD -Wall -Wextra $(CFLAGS) -o $@ $^

.PHONY: run
run: test
	$(WRAPPER) $(CURDIR)/test -n 1000000 -c 2

-include src/*.d

.PHONY: clean
clean:
	rm -f $(AR_LIB) $(SO_LIB) $(test_OBJS) $(test_OBJS:%.o=%.d) test
	rm -rf lib
