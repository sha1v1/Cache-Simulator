#ifndef CACHE_H
#define CACHE_H
#include "config.h"
#include <stdbool.h>
#include <stdint.h>

// Precomputed split: [ tag | set | block offset ]. Block size and set count
// must both be powers of two because decoding uses masks.
typedef struct {
    int block_size;           // bytes per block
    int num_sets;             // sets in the cache
    int offset_bits;          // log2(block_size)
    int set_bits;             // log2(num_sets)
    int tag_shift;            // offset_bits + set_bits: what the tag sits above
    unsigned int offset_mask; // keeps just the block offset bits
    unsigned int set_mask;    // keeps just the set index, once shifted down
} address_layout_t;

typedef struct {
    bool valid_bit;
    bool dirty; // changed since it was fetched; meaningful when valid
    unsigned int tag;
    uint8_t *block; // fixed slice of block_arena; binary data
    uint64_t last_access_time;
    uint64_t inserted_at; // when the current block entered this line; hits
                          // never change it, which is what FIFO needs
} line_t;

typedef struct {
    line_t *cache_lines;
    int lines_per_set;
} set_t;

typedef struct {
    set_t *cache_sets;
    int num_sets;
    int lines_per_set;
    address_layout_t layout; // how this cache reads an address apart
    uint8_t *block_arena;    // one allocation holding every line's block bytes
    uint64_t clock;          // per-cache replacement clock; advances per line touch
} cache_t;

cache_t *initialize_cache(const config_t *config);

int get_set_index(const address_layout_t *layout, unsigned int addr);
int get_block_offset(const address_layout_t *layout, unsigned int addr);
unsigned int get_tag_bits(const address_layout_t *layout, unsigned int addr);
unsigned int get_block_address(const address_layout_t *layout, int set_index, unsigned int tag);

// out_way receives the way within the set that matched, on a hit; may be NULL.
int check_cache(cache_t *cache, unsigned int addr, uint8_t *out_data, int *out_way);

line_t *handle_line_replacement(cache_t *cache, unsigned int addr, replacement_policy_t policy);
void update_cache(cache_t *cache, line_t *line, unsigned int tag_bits, const uint8_t *block_data,
                  int block_size);
void free_cache(cache_t *cache);

#endif
