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
| `--block-size N` | bytes per block; a power of two (default: 32) |
| `--config PATH` | read settings from `PATH` instead of `config.txt` |
| `-v`, `--verbose` | narrate the internals as well |
| `-q`, `--quiet` | print only what was explicitly asked for |
| `-h`, `--help` | show the usage message |

A `--config` file named on the command line has to exist; the default
`config.txt` merely being absent is not an error, since the built-in defaults
are already a usable machine.

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

## Configuration
The program reads configuration settings from `config.txt`, defining cache and memory behaviour. Any setting the file leaves out keeps its built-in default, so the simulator still runs if the file is missing. Blank lines and `#` comments are ignored. The parameters are listed below:
- `num_sets`: Number of sets in the cache. Must be a power of two, because the
  set index is masked out of the address rather than computed with a modulo.
- `main_memory_size`: Size of main memory in bytes. Must be a positive multiple
  of `block_size`, so the last block does not run past the end of memory. This
  also rules out a memory smaller than a single block.
- `block_size`: Bytes per block, and so the width of one cache line. Must be a
  power of two, for the same reason as `num_sets`: the block offset is masked out
  of an address rather than divided out of it, and only a power of two has a mask
  that isolates the right bits. At 48 bytes, 47 is `0b101111` and the masking
  would quietly return nonsense.
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
| [src/report.c](src/report.c) | presentation | every line of text the program prints |
| [src/log.c](src/log.c) | presentation | output level, for the layers that print |
| [src/sim.c](src/sim.c) | engine | read/write through the cache, and the statistics |
| [src/cache.c](src/cache.c) | engine | sets, lines, address decoding, replacement |
| [src/memory.c](src/memory.c) | engine | paged main memory, allocated on first touch |
| [src/config.c](src/config.c) | engine | defaults and the config file |

Names are snake_case throughout: functions and variables plainly, types with a
`_t` suffix so that a `cache_t *cache` or a `set_t *set` needs no contortion to
avoid shadowing its own type. Enum constants and macros are the usual upper case.

No engine file writes to stdout or stderr, or includes a header above it. An
access returns a `sim_status_t` and fills an `access_info_t`; a run's totals are
the public `stats_t` struct. Whoever is driving decides what that means and how to say
it, which is what lets a second front end be added without touching the
simulator.
