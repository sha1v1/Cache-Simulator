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
 *
 * Iterate over all Sets, initalize the valid bit, tag bits, data block and
 * the last access times for all lines within the same.
 */
void initializeSets(Set *sets, int num_sets, int lines_per_set)
{

    // outer loop initializes each set
    for (int i = 0; i < num_sets; i++)
    {
        sets[i].lines_per_set = lines_per_set;
        // if (sets[i].cache_lines) {
        //     free(sets[i].cache_lines);
        // }
        sets[i].cache_lines = (Line *)malloc(lines_per_set * sizeof(Line));
        if (!sets[i].cache_lines)
        {
            printf("Failed to allocate memory for lines in set %d\n", i);
            exit(1);
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

    printf("All sets in Cache initialized\n");
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
    if (!config)
    {
        printf("Error: Configurations have not been set.\n");
        return NULL;
    }

    //getSetIndex masks address bits, which only computes blockNumber % num_sets
    //when num_sets is a power of two. Any other value leaves an address bit in
    //neither the set index nor the tag, so two different blocks end up sharing a
    //(set, tag) label and alias onto each other's data.
    //n & (n-1) clears the lowest set bit: zero means only one bit was set.
    if (config->num_sets <= 0 || (config->num_sets & (config->num_sets - 1)) != 0)
    {
        printf("Error: num_sets must be a positive power of two (got %d)\n",
               config->num_sets);
        exit(1);
    }

    if (config->lines_per_set <= 0)
    {
        printf("Error: lines_per_set must be positive (got %d)\n",
               config->lines_per_set);
        exit(1);
    }

    Cache *cache = (Cache *)malloc(sizeof(Cache));
    if (!cache)
    {
        printf("Failed to allocate memory for Cache\n");
        exit(1);
    }
    cache->num_sets = config->num_sets;
    cache->lines_per_set = config->lines_per_set;

    Set *sets = (Set *)malloc(cache->num_sets * sizeof(Set));
    if (!sets)
    {
        printf("Failed to allocate memory for sets\n");
        exit(1);
    }

    cache->cache_sets = sets;

    // initialize all sets
    initializeSets(cache->cache_sets, cache->num_sets, cache->lines_per_set);
    printf("Cache initialized.\n");
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
int checkCache(Cache *cache, unsigned int addr, char* out_data){
    if(!cache || !cache->cache_sets){
        printf("Error: cache is not initialized\n");
        return -1;
    }

    int set_index = getSetIndex(addr, cache->num_sets);
    int tag_bits = getTagBits(addr, cache->num_sets);
    int block_offset = getBlockOffset(addr);


    Set *cur_set = &(cache->cache_sets[set_index]);

    // Check all lines in the set for a hit
    for (int i = 0; i < cur_set->lines_per_set; i++) {
        Line *line = &cur_set->cache_lines[i];

        if (cur_set->cache_lines[i].valid_bit && cur_set->cache_lines[i].tag == tag_bits) {
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
 * @param policy A string indicating the replacement policy (e.g., "LRU", "RANDOM").
 * 
 * @returns *Line: pointer to the line to be replaced/updated
 */
Line *handleLineReplacement(Cache *cache, unsigned int addr, const char *policy){

    int set_index = getSetIndex(addr, cache->num_sets);

    Set *cur_set = &(cache->cache_sets[set_index]);
    
    // Find an empty line: nothing has to be evicted to make room
    for (int i = 0; i < cur_set->lines_per_set; i++) {
        if (!(cur_set->cache_lines[i].valid_bit)) {
            printf("Set %d: loading into empty line %d\n", set_index, i);
            return &(cur_set->cache_lines[i]);
        }
    }

    // Apply replacement policy
    if (strcmp(policy, "LRU") == 0) {
        return leastRecentlyUsed(cur_set);
    } else if (strcmp(policy, "RANDOM") == 0) {
        return randomReplacement(cur_set);
    }

    printf("Error: Unknown replacement policy '%s'\n", policy);
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
void updateCache(Line *line, int tag_bits, const char *block_data)
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

/**
 * @brief Displays the current state of the Cache.
 *
 * @param cache A pointer to the Cache structure to display.
 *
 * Prints the contents of all sets and lines in the Cache in a formatted table,
 * including set indices, line indices, valid bits, tag bits, and block data.
 */
void displayCache(Cache *cache)
{
    if (!cache || !cache->cache_sets)
    {
        printf("Error: Cache is not initialized.\n");
        return;
    }

    int num_sets = cache->num_sets;
    int lines_per_set = cache->lines_per_set;

    printf("\n\n*****CACHE STATE*****\n");
    printf("Set | Line | Valid | Tag     | Block Data\n");
    printf("-----------------------------------------\n");

    for (int i = 0; i < num_sets; i++)
    {
        Set *set = &cache->cache_sets[i];
        for (int j = 0; j < lines_per_set; j++)
        {
            Line *line = &set->cache_lines[j];
            printf("%3d | %4d | %5d | %7u | ",
                   i, j, line->valid_bit, line->tag);

            //the block holds arbitrary bytes and has no terminator, so %s would
            //read past the array. Print printable ASCII as-is and stand in a
            //'.' for the rest, the way hexdump does.
            for (size_t k = 0; k < sizeof(line->block); k++)
            {
                unsigned char byte = (unsigned char)line->block[k];
                putchar((byte >= 32 && byte <= 126) ? byte : '.');
            }
            putchar('\n');
        }
    }
    printf("-----------------------------------------\n");
}
