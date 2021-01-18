# GNU Make

include config.mk
ifeq ($(CONFIGURED),)
$(error Please run "./configure" before using make)
endif

MAKEFLAGS += --jobs=$(shell nproc)
MAKEFLAGS += --no-print-directory

LIB = libmpsc
LIBS = lib/$(LIB).so \
       lib/$(LIB).a

SRCS = $(sort $(wildcard src/*.c))
OBJS = $(SRCS:src/%.c=obj/%.o)
INCLUDES = $(sort $(wildcard src/*.h))

CFLAGS_ALL = -std=c11 -MD $(CFLAGS_AUTO) $(CFLAGS)
LDLIBS_ALL = $(LDLIBS_AUTO) $(LDLIBS)

all: $(LIBS)

lib/%.so: $(OBJS) | mkdir-lib
	$(CC) -shared $(CFLAGS_ALL) $(LDFLAGS_ALL) $^ -o $@ $(LDLIBS_ALL)

lib/%.a: $(OBJS) | mkdir-lib
	$(AR) -crs $@ $^

obj/%.o: src/%.c config.mk Makefile | mkdir-obj
	$(CC) $(CFLAGS_ALL) -c -o $@ $<

INSTALL = $(CURDIR)/tools/install.sh

$(INCLUDEDIR)/%.h: src/%.h
	$(INSTALL) -D -m 644 $< $@

install-bin: $(LIB:lib/%=$(BINDIR)/%)

install-headers: $(INCLUDES:src/%=$(INCLUDEDIR)/%)

install: install-bin install-headers

TO_BE_CLEANED += lib
mkdir-lib:
	@mkdir -p lib

TO_BE_CLEANED += obj
mkdir-obj:
	@mkdir -p obj

srcdir := $(CURDIR)
include test/Makefile

-include obj/*.d

.PHONY: clean
clean:
	rm -rf $(TO_BE_CLEANED)

.PHONY: distclean
distclean: clean
	rm -f config.mk
