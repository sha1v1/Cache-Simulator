#include "../include/cache.h"
#include <stdlib.h>
#include <string.h>

static int address_layout_init(address_layout_t *layout, int block_size, int num_sets);
static line_t *random_replacement(set_t *set);
static line_t *least_recently_used(set_t *set);
static line_t *first_in_first_out(set_t *set);

static int initialize_sets(set_t *sets, int num_sets, int lines_per_set, uint8_t *block_arena,
                           int block_size) {
    // Let free_cache safely unwind a partially initialized cache.
    for (int i = 0; i < num_sets; i++) {
        sets[i].cache_lines = NULL;
        sets[i].lines_per_set = lines_per_set;
    }

    for (int i = 0; i < num_sets; i++) {
        sets[i].cache_lines = (line_t *)malloc(lines_per_set * sizeof(line_t));
        if (!sets[i].cache_lines) {
            return -1;
        }

        for (int j = 0; j < lines_per_set; j++) {
            line_t *line = &sets[i].cache_lines[j];
            line->valid_bit = false;
            line->dirty = false;
            line->tag = 0;
            line->last_access_time = 0;
            line->inserted_at = 0;
            // Each line owns a fixed slice of the shared block arena.
            line->block = block_arena + ((size_t)i * lines_per_set + j) * block_size;
            memset(line->block, 0, (size_t)block_size);
        }
    }

    return 0;
}

cache_t *initialize_cache(const config_t *config) {
    // sim_init provides detailed validation; this also protects direct callers.
    address_layout_t layout;
    if (!config || config->lines_per_set <= 0 ||
        address_layout_init(&layout, config->block_size, config->num_sets) != 0) {
        return NULL;
    }

    // Zeroed pointers make the normal destructor safe on partial failure.
    cache_t *cache = (cache_t *)calloc(1, sizeof(cache_t));
    if (!cache) {
        return NULL;
    }
    cache->num_sets = config->num_sets;
    cache->lines_per_set = config->lines_per_set;
    cache->layout = layout;

    cache->cache_sets = (set_t *)malloc((size_t)cache->num_sets * sizeof(set_t));
    if (!cache->cache_sets) {
        free_cache(cache);
        return NULL;
    }

    // All block storage is contiguous and released with one free.
    cache->block_arena = (uint8_t *)calloc((size_t)cache->num_sets * cache->lines_per_set,
                                           (size_t)layout.block_size);
    if (!cache->block_arena) {
        free_cache(cache);
        return NULL;
    }

    if (initialize_sets(cache->cache_sets, cache->num_sets, cache->lines_per_set,
                        cache->block_arena, layout.block_size) != 0) {
        free_cache(cache);
        return NULL;
    }
    return cache;
}

int get_set_index(const address_layout_t *layout, unsigned int addr) {
    return (int)((addr >> layout->offset_bits) & layout->set_mask);
}

int get_block_offset(const address_layout_t *layout, unsigned int addr) {
    return (int)(addr & layout->offset_mask);
}

// Exact integer log2 for values already known to be powers of two.
static int exact_log2(int n) {
    int bits = 0;
    while (n > 1) {
        n >>= 1;
        bits++;
    }
    return bits;
}

static bool is_power_of_two(int n) { return n > 0 && (n & (n - 1)) == 0; }

static int address_layout_init(address_layout_t *layout, int block_size, int num_sets) {
    if (!layout || !is_power_of_two(block_size) || !is_power_of_two(num_sets)) {
        return -1;
    }

    layout->block_size = block_size;
    layout->num_sets = num_sets;
    layout->offset_bits = exact_log2(block_size);
    layout->set_bits = exact_log2(num_sets);
    layout->tag_shift = layout->offset_bits + layout->set_bits;
    layout->offset_mask = (unsigned int)block_size - 1u;
    layout->set_mask = (unsigned int)num_sets - 1u;
    return 0;
}

unsigned int get_tag_bits(const address_layout_t *layout, unsigned int addr) {
    return addr >> layout->tag_shift;
}

unsigned int get_block_address(const address_layout_t *layout, int set_index, unsigned int tag) {
    return (tag << layout->tag_shift) | ((unsigned int)set_index << layout->offset_bits);
}

int check_cache(cache_t *cache, unsigned int addr, uint8_t *out_data, int *out_way) {
    if (!cache || !cache->cache_sets) {
        return -1;
    }

    int set_index = get_set_index(&cache->layout, addr);
    unsigned int tag_bits = get_tag_bits(&cache->layout, addr);
    int block_offset = get_block_offset(&cache->layout, addr);

    set_t *cur_set = &(cache->cache_sets[set_index]);

    for (int i = 0; i < cur_set->lines_per_set; i++) {
        line_t *line = &cur_set->cache_lines[i];

        if (line->valid_bit && line->tag == tag_bits) {
            if (out_data) {
                *out_data = line->block[block_offset];
            }
            if (out_way) {
                *out_way = i;
            }
            line->last_access_time = ++cache->clock;
            return 1;
        }
    }

    return 0;
}

line_t *handle_line_replacement(cache_t *cache, unsigned int addr, replacement_policy_t policy) {

    int set_index = get_set_index(&cache->layout, addr);

    set_t *cur_set = &(cache->cache_sets[set_index]);

    for (int i = 0; i < cur_set->lines_per_set; i++) {
        if (!(cur_set->cache_lines[i].valid_bit)) {
            return &(cur_set->cache_lines[i]);
        }
    }

    // No default: compiler warnings flag an unhandled policy.
    switch (policy) {
    case POLICY_LRU:
        return least_recently_used(cur_set);
    case POLICY_RANDOM:
        return random_replacement(cur_set);
    case POLICY_FIFO:
        return first_in_first_out(cur_set);
    }

    return NULL;
}

static line_t *least_recently_used(set_t *set) {
    line_t *lru_line = &set->cache_lines[0];
    for (int i = 1; i < set->lines_per_set; i++) {
        if (set->cache_lines[i].last_access_time < lru_line->last_access_time) {
            lru_line = &set->cache_lines[i];
        }
    }
    return lru_line;
}

// FIFO uses insertion time, which cache hits never update.
static line_t *first_in_first_out(set_t *set) {
    line_t *first = &set->cache_lines[0];
    for (int i = 1; i < set->lines_per_set; i++) {
        if (set->cache_lines[i].inserted_at < first->inserted_at) {
            first = &set->cache_lines[i];
        }
    }
    return first;
}

static line_t *random_replacement(set_t *set) {
    int line_to_replace_index = rand() % set->lines_per_set;
    return &set->cache_lines[line_to_replace_index];
}

void update_cache(cache_t *cache, line_t *line, unsigned int tag_bits, const uint8_t *block_data,
                  int block_size) {
    line->valid_bit = true;
    line->dirty = false;
    line->tag = tag_bits;
    line->last_access_time = ++cache->clock;
    line->inserted_at = cache->clock;

    // Blocks are binary data, so copy their explicit size rather than using
    // strings.
    memcpy(line->block, block_data, (size_t)block_size);
}

void free_cache(cache_t *cache) {
    if (!cache)
        return;

    if (cache->cache_sets) {
        for (int i = 0; i < cache->num_sets; i++) {
            set_t *set = &(cache->cache_sets[i]);
            if (set->cache_lines) {
                free(set->cache_lines);
                set->cache_lines = NULL;
            }
        }
        free(cache->cache_sets);
        cache->cache_sets = NULL;
    }

    free(cache->block_arena);
    free(cache);
}
