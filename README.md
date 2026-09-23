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

## Usage
Run the program using
```
./build/cache_sim
```

You'll be presented with a menu:
```
--- Cache Simulator ---
1. Read from Address
2. Write to Address
3. Display Cache
4. Exit

Enter your choice:
```

### Example commands
1. Reading from an address
```
Enter address (hex): 0x100
Cache miss! Fetching data from memory...
Data at address 0x100 (loaded from memory): 112
```

2. Writing to an address
```
Enter address (hex): 0x1a
Enter value (char): f
cache miss, writing directly to memory at 0x1A
Memory at address 26 changed to 102
```

3. Display current state of cache
```
***CACHE STATE***
Set | Line | Valid | Tag     | Block Data
-----------------------------------------
  0 |    0 |     1 |       2 | p>~ZNQRqsOIW8nYga2D_Q<4B7N)_X|2
  0 |    1 |     0 |       0 | ~{UG+Y&xh_pjq$fAaQghWMrtIw">KGE
  1 |    0 |     0 |       0 | 
  1 |    1 |     0 |       0 | 
  2 |    0 |     0 |       0 | 
  2 |    1 |     0 |       0 | 
  3 |    0 |     0 |       0 | 
  3 |    1 |     0 |       0 | 
-----------------------------------------
```

### Configuration
The program reads configuration settings from `config.text`, defining cache and memory behaviour. The parameters are listed below:
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
