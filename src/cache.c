#include "../include/cache.h"
#include "../include/memory.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// counter to manage last access times for lines in a set
unsigned int globalTime = 0;

/**
 * @brief Initialize all sets within the Cache.
 *
 * @param sets An array of Sets
 * @param numSets number of Sets in the Cache
 * @param linesPerSet number of lines in a single Set
 *
 * Iterate over all Sets, initalize the valid bit, tag bits, data block and
 * the last access times for all lines within the same.
 */
void initializeSets(Set *sets, int numSets, int linesPerSet)
{

    // outer loop initializes each set
    for (int i = 0; i < numSets; i++)
    {
        sets[i].linesPerSet = linesPerSet;
        // if (sets[i].cacheLines) {
        //     free(sets[i].cacheLines);
        // }
        sets[i].cacheLines = (Line *)malloc(linesPerSet * sizeof(Line));
        if (!sets[i].cacheLines)
        {
            printf("Failed to allocate memory for lines in set %d\n", i);
            exit(1);
        }

        // now initialize each line within a set
        for (int j = 0; j < linesPerSet; j++)
        {
            sets[i].cacheLines[j].validBit = false;
            sets[i].cacheLines[j].tag = 0;
            sets[i].cacheLines[j].lastAccessTime = 0;
            memset(sets[i].cacheLines[j].block, 0, sizeof(sets[i].cacheLines[j].block));
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
 * Initialize the numSets and linesPerSet fields and then inialize all Sets by calling inializetSets().
 */
Cache *initalizeCache(Config *config)
{
    if (!config)
    {
        printf("Error: Configurations have not been set.\n");
        return NULL;
    }

    Cache *cache = (Cache *)malloc(sizeof(Cache));
    if (!cache)
    {
        printf("Failed to allocate memory for Cache\n");
        exit(1);
    }
    cache->numSets = config->numSets;
    cache->linesPerSet = config->linesPerSet;

    Set *sets = (Set *)malloc(cache->numSets * sizeof(Set));
    if (!sets)
    {
        printf("Failed to allocate memory for sets\n");
        exit(1);
    }

    cache->cacheSets = sets;

    // initialize all sets
    initializeSets(cache->cacheSets, cache->numSets, cache->linesPerSet);
    printf("Cache initialized.\n");
    return cache;
}

/**
 * @brief Given an address and number of sets, find the index of the set which the address maps to.
 *
 * @param addr The address for which the set index is to be calculated.
 * @param numSets The number os Sets in the Cache
 */
int getSetIndex(unsigned int addr, int numSets)
{
    // right shift 5 bits to get rid of block offset bits
    // Then isolate set bits.
    return (addr >> BLOCK_OFFSET_BITS) & (numSets - 1);
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
 * @brief Given an address and the number of sets, get the tag bits.
 *
 * @param addr the address from which the tag bits are to be calculated
 * @param numSets number of Sets in the Cache
 */
int getTagBits(unsigned int addr, int numSets)
{
    int setBits = (int)log2(numSets); // number of set bits
    return addr >> (BLOCK_OFFSET_BITS + setBits);
}

/**
 * In case of a cache miss, fetch the line of data from memory
 * and populate the said block
 */
// void handleCacheMiss(int addr, Line *line, int tagbits){
//     line->validBit = true;
//     line->tag = tagbits;

//     int blockStartAddress = addr & ~31;
//     for (int i = 0; i < 32; i++) {       // 32 bytes per block
//         line->block[i] = readFromMemory(memory, blockStartAddress + i);
//     }
//     printf("Loaded block from main memory to cache\n");

// }


/**
 * @brief given an address, check if we have the cached value for the same.
 * 
 * @param cache a pointer to the cache structure
 * @param addr the memory address being acessed
 * @param outData pointer to a char where the data will be written on a hit
 * @return int:
 *      - 1: Cache hit
 *      - 0: Cache miss
 *      - -1: Error
 * 
 */
int checkCache(Cache *cache, unsigned int addr, char* outData){
    if(!cache || !cache->cacheSets){
        printf("Error: cache is not initialized\n");
        return -1;
    }

    int setIndex = getSetIndex(addr, cache->numSets);
    int tagBits = getTagBits(addr, cache->numSets);
    int blockOffset = getBlockOffset(addr);


    Set *curSet = &(cache->cacheSets[setIndex]);

    // Check all lines in the set for a hit
    for (int i = 0; i < curSet->linesPerSet; i++) {
        Line *line = &curSet->cacheLines[i];

        if (curSet->cacheLines[i].validBit && curSet->cacheLines[i].tag == tagBits) {
            // Cache hit: Retrieve the data at the block offset
            if (outData) {
                *outData = line->block[blockOffset];
            }
            line->lastAccessTime = globalTime++;  // Update LRU timestamp
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

    int setIndex = getSetIndex(addr, cache->numSets);

    Set *curSet = &(cache->cacheSets[setIndex]);
    
    // Find an empty line
    for (int i = 0; i < curSet->linesPerSet; i++) {
        if (!(curSet->cacheLines[i].validBit)) {
        printf("%d\n", setIndex);
            Line* line =  &(curSet->cacheLines[i]);  // Return the empty line
            printf("%p\n", line);
            // printf("%p\n", &curSet->cacheLines);
            return line;
        printf("here8\n");
            exit(1);
        }
    }

    // Apply replacement policy
    if (strcmp(policy, "LRU") == 0) {
        return leastRecentlyUsed(curSet);
    } else if (strcmp(policy, "RANDOM") == 0) {
        return randomReplacement(curSet);
    }

    printf("Error: Unknown replacement policy '%s'\n", policy);
    return NULL;
}


/**
 * @brief Given a set, find the line to be removed using the Least Recently used policy.
 *
 * @param set a pointer to the Set
 * To do so, we make use of 'lastAccessTime' field of each Line, and select the one with the
 * least value.
 */
Line *leastRecentlyUsed(Set *set)
{
    Line *lruLine = &set->cacheLines[0];
    for (int i = 1; i < set->linesPerSet; i++)
    {
        if (set->cacheLines[i].lastAccessTime < lruLine->lastAccessTime)
        {
            lruLine = &set->cacheLines[i];
        }
    }
    return lruLine;
}

/**
 * @brief Given a set, return the Line to be evicted randomly.
 *
 * @param set a pointer to the Set
 */
Line *randomReplacement(Set *set)
{
    // this runs if an empty line wasn't found
    int lineToReplaceIndex = rand() % set->linesPerSet;
    return &set->cacheLines[lineToReplaceIndex];
}

/**
 * @brief Update a given Line with the provided tag bits and the clock of data.
 *
 * @param line pointer to the line to be updated
 * @param the the value which the tag bits are to be set to
 * @param blockData the new data for the line: must point to at least
 *        BLOCK_SIZE readable bytes, all of which are copied.
 *
 * Used after cache hit/miss to keep it consistent with the main memory.
 */
void updateCache(Line *line, int tagBits, const char *blockData)
{
    // Line *line = &(cache->cacheSets[setIndex].cacheLines);
    line->validBit = true;
    line->tag = tagBits;
    line->lastAccessTime = globalTime++; // update to current time

    //blockData is BLOCK_SIZE raw bytes, not a string: copy a fixed count so a
    //zero byte inside the block neither truncates the copy nor, in its absence,
    //lets the copy run past the end of either buffer.
    memcpy(line->block, blockData, sizeof(line->block));
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
    for (int i = 0; i < cache->numSets; i++)
    {
        Set *set = &(cache->cacheSets[i]);
        if (set->cacheLines)
        {
            free(set->cacheLines);
            set->cacheLines = NULL;
        }
    }

    free(cache->cacheSets);
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
    if (!cache || !cache->cacheSets)
    {
        printf("Error: Cache is not initialized.\n");
        return;
    }

    int numSets = cache->numSets;
    int linesPerSet = cache->linesPerSet;

    printf("\n\n*****CACHE STATE*****\n");
    printf("Set | Line | Valid | Tag     | Block Data\n");
    printf("-----------------------------------------\n");

    for (int i = 0; i < numSets; i++)
    {
        Set *set = &cache->cacheSets[i];
        for (int j = 0; j < linesPerSet; j++)
        {
            Line *line = &set->cacheLines[j];
            printf("%3d | %4d | %5d | %7u | ",
                   i, j, line->validBit, line->tag);

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
