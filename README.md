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

## Usage
Run the program using
```
./build/cache_sim
```

It prints the active configuration and the command list, then waits at a prompt:
```
cache> r 0x100
R 0x0100  MISS  set 0  value 0x47 'G'  (filled line 0)

cache> r 0x104
R 0x0104  HIT   set 0  value 0x33 '3'

cache> w 0x100 Z
W 0x0100  HIT   set 0  value 0x5A 'Z'  -> cache + memory

cache> s

Statistics
  accesses    : 3 (2 reads, 1 writes)
  hits        : 2 (66.67%)
  misses      : 1 (33.33%)
  read hits   : 1 / 2 (50.00%)
  write hits  : 1 / 1 (100.00%)
  evictions   : 0
  errors      : 0
```

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

`v` turns on a running commentary of what the simulator is doing underneath —
which memory page was allocated, which line was chosen and why — and turns it
off again.

### Statistics
`hits` and `misses` account for exactly the accesses that completed: an access
that failed, such as one to an address outside main memory, is counted in
`errors` alone. `evictions` counts only the misses that displaced a valid line,
so a cold miss into an empty line is not one.

## Configuration
The program reads configuration settings from `config.txt`, defining cache and memory behaviour. Any setting the file leaves out keeps its built-in default, so the simulator still runs if the file is missing. Blank lines and `#` comments are ignored. The parameters are listed below:
- `num_sets`: Number of sets in the cache. Must be a power of two, because the
  set index is masked out of the address rather than computed with a modulo.
- `main_memory_size`: Size of main memory in bytes. Must be a positive multiple
  of the 32-byte block size, so the last block does not run past the end of memory.
- `lines_per_set`: Number of lines in each set in the cache. Any positive value;
  associativity is not encoded in the address, so it need not be a power of two.
- `replacement_policy`: Defines the policy to replace line in case of conflicts.
    - `LRU`: replace the least recently used line.
    - `RANDOM`: replace a randomly chosen line.

Writes are write-through with no-write-allocate: a write always reaches main
memory, and updates the cache only when the address is already resident.

Main memory is not initialized from a file. A page is filled with random
printable bytes the first time it is touched, so two runs of the same commands
see different data.

## Cache state
`d` dumps every line:
```
*****CACHE STATE*****
Set | Line | Valid | Tag     | Block Data
-----------------------------------------
  0 |    0 |     1 |       2 | p>~ZNQRqsOIW8nYga2D_Q<4B7N)_X|2
  0 |    1 |     0 |       0 | ................................
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
main.c  commands.c        front end: what to do, and reading input
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
| [src/main.c](src/main.c) | front end | reads the configuration and starts the prompt |
| [src/commands.c](src/commands.c) | front end | the command language and the menu loop |
| [src/report.c](src/report.c) | presentation | every line of text the program prints |
| [src/log.c](src/log.c) | presentation | output level, for the layers that print |
| [src/sim.c](src/sim.c) | engine | read/write through the cache, and the statistics |
| [src/cache.c](src/cache.c) | engine | sets, lines, address decoding, replacement |
| [src/memory.c](src/memory.c) | engine | paged main memory, allocated on first touch |
| [src/config.c](src/config.c) | engine | defaults and the config file |

No engine file writes to stdout or stderr, or includes a header above it. An
access returns a `SimStatus` and fills an `AccessInfo`; a run's totals are the
public `Stats` struct. Whoever is driving decides what that means and how to say
it, which is what lets a second front end be added without touching the
simulator.
