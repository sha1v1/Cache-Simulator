CC = gcc
CFLAGS = -Wall -Wno-error -O2
LDLIBS = -lm

SRCDIR   = src
TESTDIR  = tests
UNITYDIR = $(SRCDIR)/unity
BUILDDIR = build

# Everything except main.c: shared by the simulator and both test binaries.
LIBSRCS = $(SRCDIR)/cache.c $(SRCDIR)/classify.c $(SRCDIR)/commands.c $(SRCDIR)/config.c \
          $(SRCDIR)/log.c $(SRCDIR)/memory.c $(SRCDIR)/report.c $(SRCDIR)/sim.c \
          $(SRCDIR)/trace.c
LIBOBJS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(LIBSRCS))

SRCS = $(SRCDIR)/main.c $(SRCDIR)/cli.c $(LIBSRCS)
OBJS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))

TARGET    = $(BUILDDIR)/cache_sim
UNITYOBJ  = $(BUILDDIR)/unity.o
TESTBINS  = $(BUILDDIR)/test_cache $(BUILDDIR)/test_memory $(BUILDDIR)/test_sim

all: $(TARGET)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(TARGET): $(OBJS) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(UNITYOBJ): $(UNITYDIR)/unity.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Test binaries land in $(BUILDDIR) alongside every other build output.
$(BUILDDIR)/test_%: $(TESTDIR)/test_%.c $(LIBOBJS) $(UNITYOBJ) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $< $(LIBOBJS) $(UNITYOBJ) $(LDLIBS)

tests: $(TESTBINS)

test: $(TESTBINS)
	$(BUILDDIR)/test_cache
	$(BUILDDIR)/test_memory
	$(BUILDDIR)/test_sim

# A second build of everything, instrumented. Kept in its own directory so the
# instrumented objects can never be linked into the ordinary binary, and so a
# switch between the two needs no clean.
SANDIR     = $(BUILDDIR)/san
SANFLAGS   = -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1
SANOBJS    = $(patsubst $(SRCDIR)/%.c,$(SANDIR)/%.o,$(SRCS))
SANLIBOBJS = $(patsubst $(SRCDIR)/%.c,$(SANDIR)/%.o,$(LIBSRCS))
SANTESTS   = $(SANDIR)/test_cache $(SANDIR)/test_memory $(SANDIR)/test_sim

$(SANDIR):
	mkdir -p $(SANDIR)

$(SANDIR)/%.o: $(SRCDIR)/%.c | $(SANDIR)
	$(CC) $(CFLAGS) $(SANFLAGS) -c $< -o $@

$(SANDIR)/unity.o: $(UNITYDIR)/unity.c | $(SANDIR)
	$(CC) $(CFLAGS) $(SANFLAGS) -c $< -o $@

$(SANDIR)/cache_sim: $(SANOBJS) | $(SANDIR)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ $(SANOBJS) $(LDLIBS)

$(SANDIR)/test_%: $(TESTDIR)/test_%.c $(SANLIBOBJS) $(SANDIR)/unity.o | $(SANDIR)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ $< $(SANLIBOBJS) $(SANDIR)/unity.o $(LDLIBS)

# -Wall cannot see a read past the end of a heap allocation, which is most of what
# this project's hand-written structures - the hash set, the block arena, the trace
# parser's pointer walk - could get wrong. halt_on_error makes the first finding
# fail the target rather than scrolling past. Leak checking comes on by default
# under ASan on Linux and stays off on macOS, which is why it is not set here.
SANENV = UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1

sanitize: $(SANTESTS) $(SANDIR)/cache_sim
	@echo "--- unit tests ---"
	@for t in $(SANTESTS); do $(SANENV) $$t || exit 1; done
	@echo "--- trace mode ---"
	@$(SANENV) $(SANDIR)/cache_sim traces/conflict.trace --size 1024 \
	    --block-size 32 --associativity 4 --memory-size 8192 >/dev/null
	@echo "--- trace mode: malformed input and refused accesses ---"
	@printf 'R 0x0\nX bad\nR\nR 0xFFFFFF\n' | $(SANENV) \
	    $(SANDIR)/cache_sim - --size 1024 --memory-size 1024 >/dev/null 2>&1 || true
	@echo "--- interactive mode ---"
	@printf 'r 0x0\nr 0x4\nw 0x4 Z\nd\ns\nc\nv\nreset\nr 0x20\nq\n' \
	    | $(SANENV) $(SANDIR)/cache_sim -i -q --size 256 --memory-size 1024 >/dev/null
	@echo "sanitizers: clean"

clean:
	rm -rf $(BUILDDIR)

.PHONY: all clean test tests sanitize
