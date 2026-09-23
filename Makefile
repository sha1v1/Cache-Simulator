CC = gcc
CFLAGS = -Wall -Wno-error -O2
LDLIBS = -lm

SRCDIR   = src
TESTDIR  = tests
UNITYDIR = $(SRCDIR)/unity
BUILDDIR = build

# Everything except main.c: shared by the simulator and both test binaries.
LIBSRCS = $(SRCDIR)/cache.c $(SRCDIR)/config.c $(SRCDIR)/memory.c
LIBOBJS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(LIBSRCS))

SRCS = $(SRCDIR)/main.c $(LIBSRCS)
OBJS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))

TARGET    = $(BUILDDIR)/cache_sim
UNITYOBJ  = $(BUILDDIR)/unity.o
TESTBINS  = $(BUILDDIR)/test_cache $(BUILDDIR)/test_memory

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

clean:
	rm -rf $(BUILDDIR)

.PHONY: all clean test tests
