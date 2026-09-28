# Cache Simulator

A configurable cache and paged-memory simulator written in C. It supports interactive inspection,
trace-driven experiments, multiple replacement and write policies, three-C miss classification,
CSV output, and reproducible workload generation.

## What the program does

The program accepts a sequence of memory reads and writes and simulates how one cache would handle
them. It does not execute a compiled program or inspect a real processor. Accesses come from either
commands entered interactively or records in a text trace.

For each access, the simulator:

1. Splits the address into tag, set index, and block offset.
2. Searches the selected cache set for the block.
3. Records a hit or miss and applies the configured allocation policy.
4. On an allocating miss, selects a line, writes back a dirty victim if necessary, and fetches the
   requested block from simulated main memory.
5. Applies the configured read or write behavior and updates the reported statistics.
6. Updates a fully associative shadow cache used to classify read misses.

The result is a reproducible model of cache placement and data movement. It is intended for learning
cache behavior and comparing configurations against the same workload.

## Features

- Direct-mapped, set-associative, and fully associative cache configurations
- LRU, FIFO, and RANDOM replacement
- Write-through or write-back behavior
- Write-allocate or no-write-allocate behavior
- Compulsory, capacity, and conflict miss statistics
- Dirty-line tracking, writeback accounting, and explicit cache flushing
- Interactive and trace-driven front ends
- Deterministic seeds, CSV output, workload generators, and experiment scripts

## Build and test

```sh
git clone https://github.com/sha1v1/Cache-Simulator.git
cd Cache-Simulator
make
make test
make sanitize
```

Build artifacts are written to `build/`. `make sanitize` rebuilds the project with AddressSanitizer
and UndefinedBehaviorSanitizer, then runs the unit tests and front-end smoke tests. CI runs the normal
suite on Linux and macOS and the sanitized suite on Linux.

## Running the simulator

The executable has two modes:

| Mode | Command | Use case |
| --- | --- | --- |
| Interactive | `./build/cache_sim --interactive [options]` | Enter reads and writes manually and inspect each result |
| Trace | `./build/cache_sim PATH [options]` | Run every record from a trace file and report aggregate statistics |

In the formal usage below, `TRACE` is a placeholder for the path to a trace file. It is **not** the
literal argument `TRACE` or `trace`, and there is no `trace` subcommand.

```sh
./build/cache_sim TRACE [options]
./build/cache_sim --interactive [options]
```

Use `-` instead of a file path to read trace records from standard input.

Examples:

```sh
# Inspect accesses interactively with the default cache.
./build/cache_sim --interactive

# Run a trace through a 1 KiB, 8-way cache.
./build/cache_sim traces/conflict.trace \
  --size 1024 --block-size 32 --associativity 8 --memory-size 8192

# Model write-back with write allocation.
./build/cache_sim traces/conflict.trace \
  --memory-size 8192 --write-policy back --write-allocate

# Read a trace from standard input and emit CSV.
./build/gen_trace sequential 4096 \
  | ./build/cache_sim - --size 4096 --memory-size 4096 --format csv
```

Running without a trace path or `--interactive` prints the usage reference and exits with a non-zero
status. A trace path and `--interactive` cannot be used together.

### Options

| Option | Description | Default |
| --- | --- | --- |
| `-i`, `--interactive` | Start the interactive command loop | — |
| `--size N` | Total cache capacity in bytes | `256` |
| `--block-size N` | Cache block size in bytes | `32` |
| `--associativity N` | Lines per set | `2` |
| `--policy LRU\|RANDOM\|FIFO` | Replacement policy | `LRU` |
| `--write-policy through\|back` | Write-through or write-back | `through` |
| `--write-allocate` | Fill the cache after a write miss | disabled |
| `--no-write-allocate` | Bypass the cache after a write miss | enabled |
| `--memory-size N` | Main-memory size in bytes | `1024` |
| `--seed N` | Seed used by RANDOM replacement and memory initialization | `1` |
| `--format human\|csv` | Trace-mode output format | `human` |
| `-v`, `--verbose` | Show internal access details | interactive default |
| `-q`, `--quiet` | Suppress informational narration | trace default |
| `-h`, `--help` | Print the usage reference | — |

The simulator derives the set count as:

```text
sets = cache size / (block size × associativity)
```

The block size and derived set count must be powers of two. Cache size must describe a whole number
of sets, and main-memory size must be a positive multiple of the block size. Invalid configurations
are rejected before allocation.

Interactive mode narrates accesses by default; trace mode never prints per-access narration.

## Cache policies

### Replacement

| Policy | Victim selection |
| --- | --- |
| `LRU` | Least recently accessed line |
| `FIFO` | Line resident in the set for the longest time; hits do not change its order |
| `RANDOM` | Pseudorandom line selected from the target set |

The configured seed makes repeated runs with the same access sequence reproducible.

### Writes

Write policy and allocation policy are independent of replacement policy and associativity.

| Configuration | Write hit | Write miss |
| --- | --- | --- |
| Write-through, no-write-allocate | Update cache and memory | Update memory only |
| Write-through, write-allocate | Update cache and memory | Fetch the block, then update cache and memory |
| Write-back, no-write-allocate | Update cache and mark it dirty | Update memory only |
| Write-back, write-allocate | Update cache and mark it dirty | Fetch the block, update it, and mark it dirty |

A dirty line is copied to memory when it is evicted, explicitly flushed, or before `reset` empties
the cache.
Writebacks transfer a full block; direct writes transfer one byte.

## Interactive mode

Interactive mode reports the address split, hit or miss, selected set and line, memory activity, and
dirty writebacks. Addresses are hexadecimal with or without an `0x` prefix. Write values are either
one character or an explicit byte such as `0x41`.

| Command | Description |
| --- | --- |
| `r <addr>` | Read one byte |
| `w <addr> <value>` | Write one byte |
| `d` | Display every cache line, including valid and dirty bits |
| `s` | Display statistics |
| `c` | Display the active configuration |
| `v` | Toggle verbose narration |
| `flush` | Copy all dirty lines to memory without emptying the cache |
| `reset` | Flush dirty lines, empty the cache, and clear statistics and miss history |
| `h` | Display command help |
| `q` | Quit |

Missing read or write arguments are prompted for in an interactive terminal. `-q` disables access
narration, while explicitly requested reports such as `d`, `s`, and `c` still print.

Main memory is allocated lazily in pages. A newly touched page receives pseudorandom printable bytes;
the same seed and access sequence produce the same values.

## Trace mode

A trace record contains an operation, hexadecimal address, and optional positive byte count:

```text
R 0x1000          # one-byte read
W 0x1004 4        # write footprint covering four bytes
I 0x400abc,8      # eight-byte instruction fetch; commas are accepted
```

Blank lines and `#` comments are ignored. `R` and `I` are reads; `W` is a write. Instruction fetches
use the same unified cache as data accesses.

A byte range produces one cache access for every block it touches, not one access per byte. A range
crossing one block boundary therefore produces two accesses. Trace writes use the fixed byte value
`0xA5`; the trace models block access locations, not application data.

Malformed, truncated, out-of-range, and I/O-failed records are counted and cause a non-zero exit.
The simulator still reports partial statistics and marks CSV output as incomplete.

### CSV output

`--format csv` emits a header and one self-describing result row:

```text
trace,size,block,assoc,policy,write_policy,write_allocate,seed,records,accesses,hits,misses,miss_rate,compulsory,capacity,conflict,evictions,writebacks,memory_writes,memory_write_bytes,errors,malformed,failed,truncated,io_errors,complete
```

## Statistics and miss classification

The simulator reports read and write hits and misses, aggregate hit rate, evictions, writebacks,
memory-write transactions and bytes, page usage, and failed accesses.

- Hits plus misses equal completed accesses. Failed accesses are counted only as errors.
- An eviction is counted only when a miss displaces a valid line.
- A writeback is counted when a dirty block is copied to memory.
- Dirty lines still resident when a trace ends are not automatically flushed and do not yet count as
  writebacks.
- Compulsory, capacity, and conflict counters classify read misses only. Write misses are reported
  separately, but allocating writes update the classification history used by later reads.

Three-C classification uses a fully associative LRU shadow cache with the same number of lines as the
real cache:

- **Compulsory:** the block has not previously been brought into the classification history.
- **Capacity:** a previously seen block misses in both the real and shadow caches.
- **Conflict:** a previously seen block misses in the real cache while the shadow cache hits.

This breakdown is exact when the real cache uses LRU. With FIFO or RANDOM, the real and shadow caches
also differ in replacement order, so a replacement-policy miss can be reported as conflict. Overall
hits, misses, evictions, and writebacks remain exact; only the capacity-versus-conflict explanation is
ambiguous.

The shadow cache uses a linear fully associative lookup, so classification cost grows with cache
capacity.

## Workload generation

`build/gen_trace` writes traces to standard output:

```sh
./build/gen_trace WORKLOAD [parameters...]
```

| Workload | Parameters | Purpose |
| --- | --- | --- |
| `sequential` | `BYTES [WORD]` | Spatial locality and block-size effects |
| `loop` | `BYTES PASSES [WORD]` | Reuse and capacity pressure |
| `strided` | `BYTES STRIDE [COUNT] [WORD]` | Set conflicts from regular strides |
| `random` | `BYTES COUNT [SEED] [WORD]` | Low-locality baseline |
| `matmul-naive` | `N [ELEMENT]` | Conventional matrix-multiplication access order |
| `matmul-tiled` | `N TILE [ELEMENT]` | Blocked matrix multiplication |

`WORD` defaults to 4 bytes and `ELEMENT` to 8 bytes. Generated traces include their parameters and
memory footprint as comments.

## Reproducible experiments

The repository includes two experiment drivers:

```sh
# Hold cache capacity fixed while sweeping associativity.
scripts/sweep-associativity traces/conflict.trace

# Compare naive and tiled matrix-multiplication traces.
scripts/compare-matmul
```

For `traces/conflict.trace`, a fixed 1 KiB cache produces the following result:

| Associativity | Sets | Hits | Misses | Compulsory | Capacity | Conflict |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 32 | 0 | 32 | 8 | 0 | 24 |
| 2 | 16 | 0 | 32 | 8 | 0 | 24 |
| 4 | 8 | 0 | 32 | 8 | 0 | 24 |
| 8 | 4 | 24 | 8 | 8 | 0 | 0 |
| 16 | 2 | 24 | 8 | 8 | 0 | 0 |
| 32 | 1 | 24 | 8 | 8 | 0 | 0 |

The workload contains eight repeatedly accessed blocks that map to the same set. Eight-way
associativity removes the conflict misses without changing total cache capacity.

Shell presets in `configs/` provide reusable cache configurations and accept additional arguments:

```sh
./configs/l1-32k-8way --interactive
./configs/l1-32k-8way traces/conflict.trace --policy FIFO
```

## File guide

| Path | Responsibility |
| --- | --- |
| `Makefile` | Builds the simulator, generator, tests, and sanitizer variants |
| `include/sim.h` | Public simulator API, status codes, access results, and statistics |
| `include/config.h` | Configuration structure, defaults, and policy enums |
| `include/cache.h`, `memory.h`, `classify.h` | Core data structures and module interfaces |
| Other files in `include/` | Interfaces for the CLI, command loop, trace runner, reporting, and logging |
| `src/main.c` | Program entry point; selects a mode and owns simulator startup and shutdown |
| `src/sim.c` | Simulation orchestration, access semantics, and statistics |
| `src/cache.c` | Address decoding, cache storage, and replacement |
| `src/memory.c` | Lazily allocated paged main memory |
| `src/classify.c` | Three-C miss classification and shadow-cache state |
| `src/trace.c` | Trace parsing and execution |
| `src/commands.c` | Interactive command loop |
| `src/cli.c` | Command-line parsing and configuration construction |
| `src/report.c` | Human-readable and CSV presentation |
| `src/log.c` | Quiet, normal, and verbose output filtering |
| `src/config.c` | Defaults, policy parsing, and derived cache geometry |
| `src/gen_trace.c` | Standalone synthetic workload generator |
| `src/unity/` | Vendored Unity test framework |
| `tests/` | Unit tests and generator integration tests |
| `traces/` | Small committed traces for demonstrations and regression checks |
| `scripts/` | Reproducible experiment drivers |
| `configs/` | Executable cache-configuration presets |
| `.github/workflows/ci.yml` | Linux/macOS build tests and Linux sanitizer checks |

The simulation engine does not print directly. It returns status, access details, and statistics to
the front ends, which keeps simulation behavior independent from presentation.

## Scope

This project models cache placement and data movement, not timing. It does not simulate cycles,
latency, multilevel caches, coherence, prefetching, or separate instruction and data caches. Main
memory contents are synthetic rather than loaded from a program image.
