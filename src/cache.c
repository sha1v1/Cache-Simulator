#include "../include/cache.h"
#include "../include/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// counter to manage last access times for lines in a set
unsigned int global_time = 0;

/**
 * @brief Initialize all sets within the Cache.
 *
 * @param sets An array of Sets
 * @param num_sets number of Sets in the Cache
 * @param lines_per_set number of lines in a single Set
 * @return int Returns 0 on success, or -1 if a set's lines could not be
 *         allocated. Sets already built are left for the caller to free.
 *
 * Iterate over all Sets, initalize the valid bit, tag bits, data block and
 * the last access times for all lines within the same.
 */
int initializeSets(Set *sets, int num_sets, int lines_per_set)
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
        sets[i].cache_lines = (Line *)malloc(lines_per_set * sizeof(Line));
        if (!sets[i].cache_lines)
        {
            return -1;
        }

        // now initialize each line within a set
        for (int j = 0; j < lines_per_set; j++)
        {
            sets[i].cache_lines[j].valid_bit = false;
            sets[i].cache_lines[j].tag = 0;
            sets[i].cache_lines[j].last_access_time = 0;
            memset(sets[i].cache_lines[j].block, 0, sizeof(sets[i].cache_lines[j].block));
        }
    }

    return 0;
}

/*
    Given a configuration object(struct lol), initialize the cache by initializing all the sets
    in the same by calling initializeSets().
*/
/**
 * @brief Initialize the Cache structure based on user input from the Config struct.
 *
 * @param config a pointer to the Config structure
 * @returns cache: a pointer to the initialized Cache structure
 *
 * Initialize the num_sets and lines_per_set fields and then inialize all Sets by calling inializetSets().
 */
Cache *initalizeCache(Config *config)
{
    //simInit validates the configuration and can name the setting at fault.
    //This guard only refuses an unusable one, so that a caller reaching straight
    //for the cache still cannot build a shape the address decoding cannot
    //describe: getSetIndex masks address bits, which only computes
    //blockNumber % num_sets when num_sets is a power of two. Any other value
    //leaves an address bit in neither the set index nor the tag, so two
    //different blocks share a (set, tag) label and alias onto each other's data.
    //n & (n-1) clears the lowest set bit: zero means only one bit was set.
    if (!config
        || config->num_sets <= 0
        || (config->num_sets & (config->num_sets - 1)) != 0
        || config->lines_per_set <= 0)
    {
        return NULL;
    }

    Cache *cache = (Cache *)malloc(sizeof(Cache));
    if (!cache)
    {
        return NULL;
    }
    cache->num_sets = config->num_sets;
    cache->lines_per_set = config->lines_per_set;

    Set *sets = (Set *)malloc(cache->num_sets * sizeof(Set));
    if (!sets)
    {
        free(cache);
        return NULL;
    }

    cache->cache_sets = sets;

    // initialize all sets
    if (initializeSets(cache->cache_sets, cache->num_sets, cache->lines_per_set) != 0)
    {
        //freeCache walks every set, and initializeSets NULLed the ones it never
        //reached, so the partial cache is released the same way a whole one is
        freeCache(cache);
        return NULL;
    }
    return cache;
}

/**
 * @brief Given an address and number of sets, find the index of the set which the address maps to.
 *
 * @param addr The address for which the set index is to be calculated.
 * @param num_sets The number os Sets in the Cache
 */
int getSetIndex(unsigned int addr, int num_sets)
{
    // right shift 5 bits to get rid of block offset bits
    // Then isolate set bits.
    return (addr >> BLOCK_OFFSET_BITS) & (num_sets - 1);
}

/**
 * @brief Given an address, find the block offset to store the data.
 *
 * @param addr The address of which the offset is to be calculated
 */
int getBlockOffset(unsigned int addr)
{
    // 32 bytes/line => 5 bits to represent
    return addr & BLOCK_MASK;
}

/**
 * @brief Number of address bits needed to index num_sets sets.
 *
 * @param num_sets number of Sets in the Cache; must be a power of two
 *
 * Integer equivalent of log2(). Counting the shifts keeps this exact, where a
 * floating point log2() would have to be truncated back to an int and any
 * platform returning 2.9999.. for log2(8) would silently size the field wrong.
 */
static int setIndexBits(int num_sets)
{
    int bits = 0;
    while (num_sets > 1)
    {
        num_sets >>= 1;
        bits++;
    }
    return bits;
}

/**
 * @brief Given an address and the number of sets, get the tag bits.
 *
 * @param addr the address from which the tag bits are to be calculated
 * @param num_sets number of Sets in the Cache
 */
int getTagBits(unsigned int addr, int num_sets)
{
    return addr >> (BLOCK_OFFSET_BITS + setIndexBits(num_sets));
}

/**
 * In case of a cache miss, fetch the line of data from memory
 * and populate the said block
 */
// void handleCacheMiss(int addr, Line *line, int tagbits){
//     line->valid_bit = true;
//     line->tag = tagbits;

//     int block_start_address = addr & ~31;
//     for (int i = 0; i < 32; i++) {       // 32 bytes per block
//         line->block[i] = readFromMemory(memory, block_start_address + i);
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
int checkCache(Cache *cache, unsigned int addr, uint8_t* out_data){
    if(!cache || !cache->cache_sets){
        return -1;
    }

    int set_index = getSetIndex(addr, cache->num_sets);
    int tag_bits = getTagBits(addr, cache->num_sets);
    int block_offset = getBlockOffset(addr);


    Set *cur_set = &(cache->cache_sets[set_index]);

    // Check all lines in the set for a hit
    for (int i = 0; i < cur_set->lines_per_set; i++) {
        Line *line = &cur_set->cache_lines[i];

        //tag is unsigned while getTagBits returns int; cast so the comparison
        //is not done in unsigned arithmetic behind the reader's back
        if (cur_set->cache_lines[i].valid_bit && cur_set->cache_lines[i].tag == (unsigned int)tag_bits) {
            // Cache hit: Retrieve the data at the block offset
            if (out_data) {
                *out_data = line->block[block_offset];
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
 * @returns *Line: pointer to the line to be replaced/updated
 */
Line *handleLineReplacement(Cache *cache, unsigned int addr, ReplacementPolicy policy){

    int set_index = getSetIndex(addr, cache->num_sets);

    Set *cur_set = &(cache->cache_sets[set_index]);
    
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
        case POLICY_LRU:    return leastRecentlyUsed(cur_set);
        case POLICY_RANDOM: return randomReplacement(cur_set);
    }

    return NULL;
}


/**
 * @brief Given a set, find the line to be removed using the Least Recently used policy.
 *
 * @param set a pointer to the Set
 * To do so, we make use of 'last_access_time' field of each Line, and select the one with the
 * least value.
 */
Line *leastRecentlyUsed(Set *set)
{
    Line *lru_line = &set->cache_lines[0];
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
 * @brief Given a set, return the Line to be evicted randomly.
 *
 * @param set a pointer to the Set
 */
Line *randomReplacement(Set *set)
{
    // this runs if an empty line wasn't found
    int line_to_replace_index = rand() % set->lines_per_set;
    return &set->cache_lines[line_to_replace_index];
}

/**
 * @brief Update a given Line with the provided tag bits and the clock of data.
 *
 * @param line pointer to the line to be updated
 * @param the the value which the tag bits are to be set to
 * @param block_data the new data for the line: must point to at least
 *        BLOCK_SIZE readable bytes, all of which are copied.
 *
 * Used after cache hit/miss to keep it consistent with the main memory.
 */
void updateCache(Line *line, int tag_bits, const uint8_t *block_data)
{
    // Line *line = &(cache->cache_sets[set_index].cache_lines);
    line->valid_bit = true;
    line->tag = tag_bits;
    line->last_access_time = global_time++; // update to current time

    //block_data is BLOCK_SIZE raw bytes, not a string: copy a fixed count so a
    //zero byte inside the block neither truncates the copy nor, in its absence,
    //lets the copy run past the end of either buffer.
    memcpy(line->block, block_data, sizeof(line->block));
}

/**
 * @brief Frees all memory occupied by the Cache structure.
 *
 * @param memory A pointer to the Cache structure to free.
 *
 * The function fress all Sets and Lines.
 * After calling this, the Cache structure will be in an
 * uninitialized state.
 */
void freeCache(Cache *cache)
{
    if (!cache)
        return; // Ensure the cache pointer is valid

    // Free the array of sets
    for (int i = 0; i < cache->num_sets; i++)
    {
        Set *set = &(cache->cache_sets[i]);
        if (set->cache_lines)
        {
            free(set->cache_lines);
            set->cache_lines = NULL;
        }
    }

    free(cache->cache_sets);
    free(cache);
}
