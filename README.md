# Cache-Simulator
This project models the interaction between main memory and cache. The simulator helps visualize cache hits, misses, and replacement strategies, providing insights into the efficiency of caching mechanisms in a computer system.

The project supports various cache cofigurations, replacement policies and memory operations. This is a command line tool written in C, providing a structured approach to understanding cache behavior.

## Installation
1. Clone the repository:
```
git clone https://github.com/sha1v1/Cache-Simulator.git
```

2. Compile the project:
```
make
```

3. Run the tests (optional):
```
make test
```

4. Run them again instrumented (optional):
```
make sanitize
```

`make sanitize` rebuilds everything with AddressSanitizer and
UndefinedBehaviorSanitizer into `build/san/`, then runs the suite plus a smoke
pass over both front ends, including malformed trace input. `-Wall` cannot see a
read past the end of a heap allocation, and most of what the hand-written
structures here could get wrong is exactly that: the open-addressed hash set, the
block arena every cache line points into, and the trace parser's pointer walk. An
off-by-one deliberately introduced into the shadow cache's scan produced no
compiler warning at all and a bare `exit 138` from the test binary; under the
sanitizers it came back as `heap-buffer-overflow classify.c:177 in
classifier_access`.

Both run in CI on Linux and macOS, on every branch.

## Usage
The simulator is built to carry more than one front end, so it has to be told
which one to run. Interactive mode steps through accesses by hand and narrates
each one:

```
./build/cache_sim --interactive
```

Run with no arguments it prints the usage message and exits non-zero, rather
than guessing which front end was meant.

### Options
| Option | Meaning |
| --- | --- |
| `-i`, `--interactive` | step through accesses one command at a time |
| `--size N` | total cache size in bytes (default: 256) |
| `--block-size N` | bytes per block; a power of two (default: 32) |
| `--associativity N` | lines per set (default: 2) |
| `--policy LRU\|RANDOM\|FIFO` | replacement policy (default: LRU) |
| `--memory-size N` | bytes of main memory (default: 1024) |
| `--seed N` | seed for RANDOM replacement (default: 1) |
| `-v`, `--verbose` | narrate the internals as well |
| `-q`, `--quiet` | print only what was explicitly asked for |
| `-h`, `--help` | show the usage message |

The number of sets is **derived**, not given: `size / (block-size × associativity)`.
That is what lets a comparison hold total capacity fixed and vary only the
organisation — `--size 32768 --associativity 1` and `--size 32768 --associativity 8`
are two shapes of the same 32 KB. A size that is not a whole number of sets is
refused, and the message names the nearest sizes that are.

The command line is the only way to configure a run: there is no settings file, so
a result depends on the arguments alone and cannot change with the directory the
program was started from.

Interactive mode prints the active configuration and the command list, then
waits at a prompt. Each access is narrated in three parts: how the address
divides into the fields the cache actually uses, what the cache did with it,
and what main memory was asked for.

```
cache>   0x100 = tag 0x2 | set 0 | offset 0   (25|2|5 bits)
R 0x0100  MISS  set 0  value 0x2E '.'  (filled way 0, which was free)
  memory page 1 (allocated by this access)
  fetched all 32 bytes of the block, not just the byte asked for

cache>   0x104 = tag 0x2 | set 0 | offset 4   (25|2|5 bits)
R 0x0104  HIT   set 0  value 0x5C '\'  (served from way 0)
  main memory not consulted

cache>   0x100 = tag 0x2 | set 0 | offset 0   (25|2|5 bits)
W 0x0100  HIT   set 0  value 0x5A 'Z'  -> cache way 0 + memory
  memory page 1

cache>   0x180 = tag 0x3 | set 0 | offset 0   (25|2|5 bits)
R 0x0180  MISS  set 0  value 0x49 'I'  (filled way 1, which was free)
  memory page 1
  fetched all 32 bytes of the block, not just the byte asked for

cache>   0x200 = tag 0x4 | set 0 | offset 0   (25|2|5 bits)
R 0x0200  MISS  set 0  value 0x6D 'm'  (filled way 1, evicting a valid line)
  memory page 2 (allocated by this access)
  fetched all 32 bytes of the block, not just the byte asked for

cache> 
Statistics
  accesses    : 5 (4 reads, 1 writes)
  hits        : 2 (40.00%)
  misses      : 3 (60.00%)
  read hits   : 1 / 4 (25.00%)
  write hits  : 1 / 1 (100.00%)
  evictions   : 1
  pages used  : 2
  errors      : 0
```

Reading `0x104` hits because the miss on `0x100` brought in the whole block
around it, not just the byte asked for. Widening `--block-size` widens that
effect: at 64 bytes `0x120` would hit too, where at 32 it lands in another block
and another set. `0x180` maps to the same set but
carries a different tag, so it fills the set's other way; `0x200` is a third
block competing for those two ways, so something has to go.

### Commands
| Command | Meaning |
| --- | --- |
| `r <addr>` | read a byte, e.g. `r 0x100` |
| `w <addr> <value>` | write a byte, e.g. `w 0x1a f` or `w 0x1a 0x41` |
| `d` | display the cache |
| `s` | show statistics |
| `c` | show the configuration |
| `v` | toggle verbose narration of the internals |
| `reset` | empty the cache and clear the statistics |
| `h` | show the command list |
| `q` | quit |

Addresses are hex, with or without an `0x` prefix, so `r 100` and `r 0x100` are
the same address. A value is a single character, or `0xNN` for a byte that is
awkward to type. Arguments left off are prompted for, so plain `r` still works,
as do the original menu numbers `1`–`4`.

Interactive mode narrates the internals by default: which memory page was
touched, whether that access is what brought it into existence, and that a miss
fetches an entire block. `v` toggles that commentary off and back on, and `-q`
starts without it.

### Statistics
`hits` and `misses` account for exactly the accesses that completed: an access
that failed, such as one to an address outside main memory, is counted in
`errors` alone. `evictions` counts only the misses that displaced a valid line,
so a cold miss into an empty line is not one.

## Trace mode
Interactive mode shows how one access works. Trace mode runs a whole workload and
reports only the totals, which is what makes configurations comparable:

```
./build/cache_sim traces/conflict.trace --size 1024 --block-size 32 --associativity 8
cat traces/conflict.trace | ./build/cache_sim - --size 1024      # - reads stdin
```

A trace line is an operation, an address, and optionally how many bytes it
touches. Addresses are hex, with or without `0x`; fields may be separated by
spaces or commas; blank lines and `#` comments are ignored.

```
R 0x1000          read one byte
W 0x1004 4        write four bytes
I 0x400abc,8      an instruction fetch
```

The byte count matters. An access that straddles a block boundary touches two
blocks and is therefore **two** cache lookups, each able to hit or miss on its
own — counting it once would undercount exactly the unaligned accesses real
programs make. `records` and `accesses` in the output differ for this reason.

Instruction fetches go through the same cache as data, since there is only one
here, but are counted separately so a trace that distinguishes them is not
silently flattened. A write stores a fixed byte: a trace records where a program
wrote, not what, and the value cannot turn a hit into a miss.

### Nothing is silently wrong
A line that cannot be read, or an access the simulator refuses — an address past
the end of main memory, say — is counted, the first few are named with their line
numbers, and **the run exits non-zero**. The statistics still print, marked
incomplete. A number computed from a trace that was only partly read must not be
mistaken for one from a trace that was read whole.

### Machine-readable output
`--format csv` prints one row carrying both the results and the configuration that
produced them, so a file of them is self-describing:

```
trace,size,block,assoc,policy,seed,records,accesses,hits,misses,miss_rate,compulsory,capacity,conflict,evictions,errors,malformed,failed,truncated,io_errors,complete
traces/conflict.trace,1024,32,8,LRU,1,32,32,24,8,0.250000,8,0,0,0,0,0,0,0,0,true
```

### A worked result
[traces/conflict.trace](traces/conflict.trace) reads eight blocks 1024 bytes
apart, four times over. The stride is chosen so all eight map to set 0 whatever
the set count is, which isolates associativity from everything else.
[scripts/sweep-associativity](scripts/sweep-associativity) runs it at a **fixed
1 KB of cache** across every associativity that size allows:

| associativity | sets | hits | misses | compulsory | capacity | conflict |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 32 | 0 | 32 | 8 | 0 | 24 |
| 2 | 16 | 0 | 32 | 8 | 0 | 24 |
| 4 | 8 | 0 | 32 | 8 | 0 | 24 |
| 8 | 4 | 24 | **8** | 8 | 0 | **0** |
| 16 | 2 | 24 | 8 | 8 | 0 | 0 |
| 32 | 1 | 24 | 8 | 8 | 0 | 0 |

Read the last three columns rather than the miss count, because they say which
knob to turn:

- **compulsory is 8 in every row.** It is the trace's footprint — eight distinct
  blocks, each fetched once — and no cache shape changes it. Its being constant is
  what proves the rows are comparable at all.
- **capacity is 0 everywhere.** Eight blocks into a cache holding thirty-two: the
  room was never the problem, so a bigger cache would have bought nothing.
- **conflict is 24, then 0.** Below eight ways the blocks evict one another
  although most of the cache stands empty, because all of them may only live in
  set 0. At eight ways they all fit and it vanishes entirely.

So the diagnosis is *add associativity, not capacity* — and that is a conclusion a
miss rate alone cannot support. The 25% floor at the bottom is exactly the eight
compulsory misses over thirty-two accesses; more associativity past that point
buys nothing, which is why the curve goes flat rather than continuing down.

### How the breakdown is worked out
Telling capacity from conflict needs a reference that cannot have conflict misses,
so [src/classify.c](src/classify.c) runs a **fully associative cache of the same
total size** alongside the real one, holding tags only. If it would have hit, the
room existed and the set mapping wasted it: conflict. If it would have missed too,
the room genuinely was not there: capacity. Telling compulsory from either needs
the set of blocks ever brought in, which grows with the trace's footprint rather
than with the cache.

Two consequences worth knowing:

- The three account for the **read** misses. A write miss under no-write-allocate
  fills nothing, so it has no placement decision to attribute; write misses are
  reported separately.
- The breakdown is exact when the real cache also uses `LRU`. With `RANDOM` or
  `FIFO`, the fully associative reference remains LRU, so a replacement-policy
  miss can appear in the `conflict` column. The overall hits, misses and evictions
  remain exact; only the explanation of a non-compulsory read miss is ambiguous.
- The reference cache is fully associative, so finding a block in it means a linear
  scan of every block the cache holds. That costs about 0.6s for 500k accesses at
  32 KB and 3s at 256 KB. Fine at these sizes; a hash index over the ways is the
  way out if it ever is not.

## Generating workloads
[src/gen_trace.c](src/gen_trace.c) writes traces to standard output. It is a
separate program that knows nothing about caches, deliberately: a trace describes
what a program touches, and if it changed with the cache then every row of a sweep
would be a different experiment.

```
./build/gen_trace WORKLOAD [parameters...]
./build/gen_trace matmul-tiled 64 8 | ./build/cache_sim - --size 4096
```

| workload | parameters | what it exercises |
| --- | --- | --- |
| `sequential` | `BYTES [WORD]` | spatial locality; block size matters, associativity does not |
| `loop` | `BYTES PASSES [WORD]` | **capacity** misses when the region does not fit |
| `strided` | `BYTES STRIDE [COUNT] [WORD]` | **conflict** misses when the stride is one set's span |
| `random` | `BYTES COUNT [SEED] [WORD]` | no locality; the floor everything else beats |
| `matmul-naive` | `N [ELEMENT]` | textbook loop order over three N×N matrices |
| `matmul-tiled` | `N TILE [ELEMENT]` | the same arithmetic, blocked |

Each trace opens with a comment naming the parameters it was made from and closes
with one naming its footprint, so `--memory-size` can be chosen without guessing.

### Each kind of miss, on demand
The breakdown is only worth having if it can distinguish the cases, so each is
reproducible. A 4 KB cache with 64-byte blocks throughout:

| command | miss rate | compulsory | capacity | conflict |
| --- | --- | --- | --- | --- |
| `sequential 4096` | 6.3% | 64 | 0 | 0 |
| `loop 4096 4` at 2 KB | 6.3% | 64 | **192** | 0 |
| `loop 4096 4` at 2 KB, fully associative | 6.3% | 64 | **192** | 0 |
| `strided 8192 512 64` 4-way | 100% | 16 | 0 | **48** |
| `strided 8192 512 64` fully associative | 25% | 16 | 0 | **0** |

The pairs are the point. Making the cache fully associative leaves the capacity
misses untouched — placement freedom cannot manufacture space — while it removes
the conflict misses entirely, because those were only ever the mapping's fault.

### Tiling is two fixes, not one
`scripts/compare-matmul` multiplies three N×N matrices of 8-byte doubles, 96 KB of
data, through a **4 KB 4-way** cache:

| workload | miss rate | compulsory | capacity | conflict |
| --- | --- | --- | --- | --- |
| naive, N=64 | 51.7% | 1,536 | **269,696** | 0 |
| tiled, N=64 | 34.2% | 1,536 | 4,480 | **162,880** |
| naive, N=65 | 45.7% | 1,585 | 248,787 | 0 |
| tiled, N=65 | **6.3%** | 1,585 | 13,177 | 22,862 |

Read down the last two columns:

1. **Naive is capacity bound.** 270k capacity misses and not one conflict miss. The
   inner loop walks `B` down a column, and by the time the next `j` wants those
   blocks they have been evicted. No amount of associativity would help.
2. **Tiling fixes that — and exposes something else.** Capacity misses fall 60×, to
   4,480. But the miss rate only improves from 51.7% to 34.2%, because 163k
   *conflict* misses appear in their place. With N=64 a row is 512 bytes, exactly
   eight blocks, so every row of a tile lands on the same set and the tile evicts
   itself.
3. **One extra column collects the rest.** At N=65 a row is 520 bytes, which
   spreads a tile across sets. 6.3%.

51.7% to 6.3% took two separate changes addressing two different causes, and the
miss rate alone identifies neither. Sweeping associativity on the tiled N=64 trace
makes the second point sharper still — it gets *worse* from 1-way to 8-way, because
reducing the set count at fixed capacity aligns the tile rows ever more tightly,
before collapsing to 1.7% at 16-way once a tile finally fits in one set.

## Configuration
Every setting is a command line flag, listed under [Options](#options) above.
The rules each one has to satisfy:

- `--size`: Total bytes of cache. Must be a whole number of sets, i.e. divisible
  by `block-size × associativity`, since there is no fraction of a set index.
- `--block-size`: Bytes per block, and so the width of one cache line. Must be a
  power of two: the block offset is masked out of an address rather than divided
  out of it, and only a power of two has a mask that isolates the right bits. At
  48 bytes, 47 is `0b101111` and the masking would quietly return nonsense.
- `--associativity`: Lines in each set. Any positive value; associativity is not
  encoded in the address, so it need not be a power of two. The derived set count
  must be, and is, since size and block size both are.
- `--memory-size`: Bytes of main memory. Must be a positive multiple of
  `block-size`, so the last block does not run past the end of memory. This also
  rules out a memory smaller than a single block.
- `--policy`: `LRU` replaces the least recently used line, `RANDOM` one chosen at
  random, and `FIFO` the line that has been resident in its set the longest. A hit
  refreshes LRU order but never FIFO insertion order.
- `--seed`: Seeds the generator `RANDOM` draws from, so two runs of the same
  command agree. It is reported with the configuration, which is what makes a
  published number reproducible.

### Presets
A long invocation is worth naming. [configs/](configs/) holds shell scripts rather
than a settings file, because a script composes — anything written after it
overrides it:

```
./configs/l1-32k-8way --interactive
./configs/l1-32k-8way --interactive --associativity 1   # same 32 KB, direct mapped
```

Writes are write-through with no-write-allocate: a write always reaches main
memory, and updates the cache only when the address is already resident.

Main memory is not initialized from a file. A page is filled with random
printable bytes the first time it is touched, so two runs of the same commands
see different data.

## Cache state
`d` dumps every line:
```
*****CACHE STATE*****
Set | Way  | Valid | Tag     | Block Data
-----------------------------------------
  0 |    0 |     1 |       2 | ZE~@\9pwz[W (,R$wu}Ku=p-sHq;.[/E
  0 |    1 |     1 |       4 | m/m4]n'u:V:U+i:Tp#E#`Gf{2'z2` 0\
  1 |    0 |     0 |       0 | ................................
  1 |    1 |     0 |       0 | ................................
  2 |    0 |     0 |       0 | ................................
  2 |    1 |     0 |       0 | ................................
  3 |    0 |     0 |       0 | ................................
  3 |    1 |     0 |       0 | ................................
-----------------------------------------
```
A block holds arbitrary bytes and has no terminator, so unprintable bytes are
shown as `.` the way `hexdump` does.

## Layout
The engine knows nothing about how it is driven, and the front end knows nothing
about how the cache works. Dependencies only ever point downward:

```
main.c  cli.c  commands.c   front end: arguments, what to do, reading input
             |
         report.c         presentation: turning results into text
             |
          sim.c           engine: accesses, statistics  (silent)
             |
   cache.c  memory.c      engine: mechanism             (silent)
             |
         config.c         engine: settings              (silent)
```

| Path | Layer | Contents |
| --- | --- | --- |
| [src/main.c](src/main.c) | front end | picks a front end from the arguments, then builds the simulator |
| [src/cli.c](src/cli.c) | front end | what the command line arguments mean |
| [src/commands.c](src/commands.c) | front end | the command language and the menu loop |
| [src/trace.c](src/trace.c) | front end | reading a trace file and running it |
| [src/gen_trace.c](src/gen_trace.c) | standalone | emits workload traces; links nothing else |
| [src/report.c](src/report.c) | presentation | every line of text the program prints |
| [src/log.c](src/log.c) | presentation | output level, for the layers that print |
| [src/sim.c](src/sim.c) | engine | read/write through the cache, and the statistics |
| [src/cache.c](src/cache.c) | engine | sets, lines, address decoding, replacement |
| [src/memory.c](src/memory.c) | engine | paged main memory, allocated on first touch |
| [src/config.c](src/config.c) | engine | defaults, policy names, derived set count |

Names are snake_case throughout: functions and variables plainly, types with a
`_t` suffix so that a `cache_t *cache` or a `set_t *set` needs no contortion to
avoid shadowing its own type. Enum constants and macros are the usual upper case.

No engine file writes to stdout or stderr, or includes a header above it. An
access returns a `sim_status_t` and fills an `access_info_t`; a run's totals are
the public `stats_t` struct. Whoever is driving decides what that means and how to say
it, which is what lets a second front end be added without touching the
simulator.
