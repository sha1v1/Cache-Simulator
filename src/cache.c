#include "../include/cache.h"
#include "../include/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// counter to manage last access times for lines in a set
unsigned int global_time = 0;

/**
 * @brief Initialize all sets within the cache.
 *
 * @param sets An array of Sets
 * @param num_sets number of sets in the cache
 * @param lines_per_set number of lines in a single set
 * @return int Returns 0 on success, or -1 if a set's lines could not be
 *         allocated. Sets already built are left for the caller to free.
 *
 * Iterate over all sets, initalize the valid bit, tag bits, data block and
 * the last access times for all lines within the same.
 */
int initialize_sets(set_t *sets, int num_sets, int lines_per_set,
                    uint8_t *block_arena, int block_size)
{
    //every set starts empty, so a caller unwinding a partial failure can free
    //the whole array without reading an uninitialized pointer
    for (int i = 0; i < num_sets; i++)
    {
        sets[i].cache_lines = NULL;
        sets[i].lines_per_set = lines_per_set;
    }

    // outer loop initializes each set
    for (int i = 0; i < num_sets; i++)
    {
        sets[i].cache_lines = (line_t *)malloc(lines_per_set * sizeof(line_t));
        if (!sets[i].cache_lines)
        {
            return -1;
        }

        // now initialize each line within a set
        for (int j = 0; j < lines_per_set; j++)
        {
            line_t *line = &sets[i].cache_lines[j];
            line->valid_bit = false;
            line->tag = 0;
            line->last_access_time = 0;
            //each line's bytes are its own slice of the one arena, laid out set by
            //set, so no line allocates or frees anything of its own
            line->block = block_arena + ((size_t)i * lines_per_set + j) * block_size;
            memset(line->block, 0, (size_t)block_size);
        }
    }

    return 0;
}

/*
    Given a configuration object(struct lol), initialize the cache by initializing all the sets
    in the same by calling initialize_sets().
*/
/**
 * @brief Initialize the cache_t structure based on user input from the config_t struct.
 *
 * @param config a pointer to the config_t structure
 * @returns cache: a pointer to the initialized cache_t structure
 *
 * Initialize the num_sets and lines_per_set fields and then inialize all Sets by calling initialize_sets().
 */
cache_t *initialize_cache(config_t *config)
{
    //sim_init validates the configuration and can name the setting at fault.
    //This guard only refuses an unusable one, so that a caller reaching straight
    //for the cache still cannot build a shape the address decoding cannot
    //describe. address_layout_init is what enforces that: both the block size and
    //the set count are masked out of an address, and any non power of two leaves
    //an address bit in neither the set index nor the tag, so two different blocks
    //share a (set, tag) label and alias onto each other's data.
    address_layout_t layout;
    if (!config
        || config->lines_per_set <= 0
        || address_layout_init(&layout, config->block_size, config->num_sets) != 0)
    {
        return NULL;
    }

    //calloc, so that every pointer free_cache might touch is NULL from the start
    //and a failure part way through can be unwound by the same code that frees a
    //whole cache
    cache_t *cache = (cache_t *)calloc(1, sizeof(cache_t));
    if (!cache)
    {
        return NULL;
    }
    cache->num_sets = config->num_sets;
    cache->lines_per_set = config->lines_per_set;
    cache->layout = layout;

    cache->cache_sets = (set_t *)malloc((size_t)cache->num_sets * sizeof(set_t));
    if (!cache->cache_sets)
    {
        free_cache(cache);
        return NULL;
    }

    //one allocation for every line's bytes rather than num_sets * lines_per_set of
    //them: a block is a fixed size and never moves, so there is nothing for a
    //per-line allocation to buy, and this way the blocks are one free
    cache->block_arena = (uint8_t *)calloc((size_t)cache->num_sets * cache->lines_per_set,
                                           (size_t)layout.block_size);
    if (!cache->block_arena)
    {
        free_cache(cache);
        return NULL;
    }

    // initialize all sets
    if (initialize_sets(cache->cache_sets, cache->num_sets, cache->lines_per_set,
                        cache->block_arena, layout.block_size) != 0)
    {
        //free_cache walks every set, and initialize_sets NULLed the ones it never
        //reached, so the partial cache is released the same way a whole one is
        free_cache(cache);
        return NULL;
    }
    return cache;
}

/**
 * @brief The set an address maps to.
 *
 * @param layout how this cache reads an address apart
 * @param addr the address to place
 *
 * Shift the block offset away, then keep only the set bits.
 */
int get_set_index(const address_layout_t *layout, unsigned int addr)
{
    return (int)((addr >> layout->offset_bits) & layout->set_mask);
}

/**
 * @brief Which byte within its block an address refers to.
 *
 * @param layout how this cache reads an address apart
 * @param addr the address whose offset is wanted
 */
int get_block_offset(const address_layout_t *layout, unsigned int addr)
{
    return (int)(addr & layout->offset_mask);
}

/**
 * @brief Number of address bits needed to index n things, for a power-of-two n.
 *
 * Integer equivalent of log2(). Counting the shifts keeps this exact, where a
 * floating point log2() would have to be truncated back to an int and any
 * platform returning 2.9999.. for log2(8) would silently size the field wrong.
 */
static int exact_log2(int n)
{
    int bits = 0;
    while (n > 1)
    {
        n >>= 1;
        bits++;
    }
    return bits;
}

//True only for a positive power of two: n & (n-1) clears the lowest set bit, so
//a zero result means that was the only bit set.
static bool is_power_of_two(int n)
{
    return n > 0 && (n & (n - 1)) == 0;
}

int address_layout_init(address_layout_t *layout, int block_size, int num_sets)
{
    if (!layout || !is_power_of_two(block_size) || !is_power_of_two(num_sets))
    {
        return -1;
    }

    layout->block_size  = block_size;
    layout->num_sets    = num_sets;
    layout->offset_bits = exact_log2(block_size);
    layout->set_bits    = exact_log2(num_sets);
    layout->tag_shift   = layout->offset_bits + layout->set_bits;
    layout->offset_mask = (unsigned int)block_size - 1u;
    layout->set_mask    = (unsigned int)num_sets - 1u;
    return 0;
}

/**
 * @brief Everything above the set field: what distinguishes blocks sharing a set.
 *
 * @param layout how this cache reads an address apart
 * @param addr the address whose tag is wanted
 *
 * Unsigned, matching line_t.tag, so the comparison on a lookup needs no cast.
 * tag_shift is precomputed, which is what keeps a log2 off the access path.
 */
unsigned int get_tag_bits(const address_layout_t *layout, unsigned int addr)
{
    return addr >> layout->tag_shift;
}

/**
 * In case of a cache miss, fetch the line of data from memory
 * and populate the said block
 */
// void handle_cache_miss(int addr, line_t *line, int tagbits){
//     line->valid_bit = true;
//     line->tag = tagbits;

//     int block_start_address = addr & ~31;
//     for (int i = 0; i < 32; i++) {       // 32 bytes per block
//         line->block[i] = read_from_memory(memory, block_start_address + i);
//     }
//     printf("Loaded block from main memory to cache\n");

// }


/**
 * @brief given an address, check if we have the cached value for the same.
 * 
 * @param cache a pointer to the cache structure
 * @param addr the memory address being acessed
 * @param out_data pointer to a char where the data will be written on a hit
 * @return int:
 *      - 1: Cache hit
 *      - 0: Cache miss
 *      - -1: Error
 * 
 */
int check_cache(cache_t *cache, unsigned int addr, uint8_t* out_data, int *out_way){
    if(!cache || !cache->cache_sets){
        return -1;
    }

    int set_index = get_set_index(&cache->layout, addr);
    unsigned int tag_bits = get_tag_bits(&cache->layout, addr);
    int block_offset = get_block_offset(&cache->layout, addr);


    set_t *cur_set = &(cache->cache_sets[set_index]);

    // Check all lines in the set for a hit
    for (int i = 0; i < cur_set->lines_per_set; i++) {
        line_t *line = &cur_set->cache_lines[i];

        if (cur_set->cache_lines[i].valid_bit && cur_set->cache_lines[i].tag == tag_bits) {
            // Cache hit: Retrieve the data at the block offset
            if (out_data) {
                *out_data = line->block[block_offset];
            }
            //which way matched is reported so a caller that then has to touch
            //this line - or describe it - need not search for the tag again
            if (out_way) {
                *out_way = i;
            }
            line->last_access_time = global_time++;  // Update LRU timestamp
            // Cache hit
            return 1;
        }
    }

    // Cache miss
    return 0;
}

/**
 * @brief Find the line that needs to be replaced/updated when a cache miss occurs
 * 
 * @param cache a pointer to the cache structure
 * @param addr The memory address being accessed.
 * @param policy Which line a full set gives up (POLICY_LRU, POLICY_RANDOM).
 * 
 * @returns *line_t: pointer to the line to be replaced/updated
 */
line_t *handle_line_replacement(cache_t *cache, unsigned int addr, replacement_policy_t policy){

    int set_index = get_set_index(&cache->layout, addr);

    set_t *cur_set = &(cache->cache_sets[set_index]);
    
    // Find an empty line: nothing has to be evicted to make room
    for (int i = 0; i < cur_set->lines_per_set; i++) {
        if (!(cur_set->cache_lines[i].valid_bit)) {
            //the caller can tell this was an empty line rather than an eviction
            //by the line's valid bit, which is still false until it is filled
            return &(cur_set->cache_lines[i]);
        }
    }

    // Apply replacement policy. No default case: -Wswitch then warns here if a
    // policy is added to the enum and this switch is not updated.
    switch (policy) {
        case POLICY_LRU:    return least_recently_used(cur_set);
        case POLICY_RANDOM: return random_replacement(cur_set);
    }

    return NULL;
}


/**
 * @brief Given a set, find the line to be removed using the Least Recently used policy.
 *
 * @param set a pointer to the set_t
 * To do so, we make use of 'last_access_time' field of each line_t, and select the one with the
 * least value.
 */
line_t *least_recently_used(set_t *set)
{
    line_t *lru_line = &set->cache_lines[0];
    for (int i = 1; i < set->lines_per_set; i++)
    {
        if (set->cache_lines[i].last_access_time < lru_line->last_access_time)
        {
            lru_line = &set->cache_lines[i];
        }
    }
    return lru_line;
}

/**
 * @brief Given a set, return the line_t to be evicted randomly.
 *
 * @param set a pointer to the set_t
 */
line_t *random_replacement(set_t *set)
{
    // this runs if an empty line wasn't found
    int line_to_replace_index = rand() % set->lines_per_set;
    return &set->cache_lines[line_to_replace_index];
}

/**
 * @brief Update a given line_t with the provided tag bits and the clock of data.
 *
 * @param line pointer to the line to be updated
 * @param tag_bits the value the line's tag is to be set to
 * @param block_data the new data for the line: must point to at least
 *        block_size readable bytes, all of which are copied.
 * @param block_size how many bytes that is
 *
 * Used after cache hit/miss to keep it consistent with the main memory.
 */
void update_cache(line_t *line, unsigned int tag_bits, const uint8_t *block_data,
                  int block_size)
{
    line->valid_bit = true;
    line->tag = tag_bits;
    line->last_access_time = global_time++; // update to current time

    //block_data is block_size raw bytes, not a string: copy a counted length so a
    //zero byte inside the block neither truncates the copy nor, in its absence,
    //lets the copy run past the end of either buffer. The count is passed in
    //because line->block is a pointer into the arena now, so sizeof would
    //silently be the width of that pointer.
    memcpy(line->block, block_data, (size_t)block_size);
}

/**
 * @brief Frees all memory occupied by the cache_t structure.
 *
 * @param memory A pointer to the cache_t structure to free.
 *
 * The function fress all Sets and Lines.
 * After calling this, the cache_t structure will be in an
 * uninitialized state.
 */
void free_cache(cache_t *cache)
{
    if (!cache)
        return; // Ensure the cache pointer is valid

    // Free the array of sets. Guarded, because initialize_cache unwinds through
    // here and may not have got as far as allocating them.
    if (cache->cache_sets)
    {
        for (int i = 0; i < cache->num_sets; i++)
        {
            set_t *set = &(cache->cache_sets[i]);
            if (set->cache_lines)
            {
                free(set->cache_lines);
                set->cache_lines = NULL;
            }
        }
        free(cache->cache_sets);
        cache->cache_sets = NULL;
    }

    //the blocks were one allocation, so they are one free, whatever became of
    //the sets above
    free(cache->block_arena);
    free(cache);
}
