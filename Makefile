CC      ?= cc
CFLAGS  ?= -Wall -Wextra -g -O2
AR      ?= ar

SRCDIR  = src
SRCS    = $(wildcard $(SRCDIR)/*.c)
OBJS    = $(SRCS:.c=.o)
LIB     = libgamut.a
HEADER  = $(SRCDIR)/gamut.h

INSPECT = tools/inspect
SEEDS   = tools/gen_seeds

.PHONY: all clean tools fuzz-build

all: $(LIB)

$(LIB): $(OBJS)
	$(AR) rcs $@ $^

$(SRCDIR)/%.o: $(SRCDIR)/%.c $(SRCDIR)/internal.h $(HEADER)
	$(CC) $(CFLAGS) -I$(SRCDIR) -c -o $@ $<

tools: $(LIB)
	$(CC) $(CFLAGS) -I$(SRCDIR) -o $(INSPECT) tools/inspect.c $(LIB)
	$(CC) $(CFLAGS) -I$(SRCDIR) -o $(SEEDS) tools/gen_seeds.c

fuzz-build: $(LIB)
	@echo "Use build.sh or .clusterfuzzlite/build.sh for fuzzer builds"

clean:
	rm -f $(OBJS) $(LIB) $(INSPECT) $(SEEDS)
	rm -f src/*.o
