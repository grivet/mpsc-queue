# GNU Make

LIB = libmpsc
LIBS = lib/$(LIB).so lib/$(LIB).a

OBJS = src/mpsc-queue.o

CFLAGS := -std=c11 -MD -Wall -Wextra -Werror $(CFLAGS)

all: $(LIBS)

lib/%.so: $(OBJS)
	@mkdir -p lib
	$(CC) -shared $(CFLAGS) $^ -o $@ $(LDLIBS)

lib/%.a: $(OBJS)
	@mkdir -p lib
	$(AR) -crs $@ $^

src/%.o: src/%.c Makefile
	$(CC) $(CFLAGS) -c -o $@ $<

srcdir := $(CURDIR)
include test/Makefile

-include src/*.d

.PHONY: clean
clean:
	rm -f $(LIBS) $(OBJS) src/*.d
