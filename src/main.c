#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../include/cache.h"
#include "../include/memory.h"
#include "../include/config.h"

Config *config;
Memory *memory;
Cache *cache;

/**
 * @brief an initial booting up of sorts to run the application
 */
void init(){
    //read the config file 
    // Allocate memory for config, memory, and cache
    config = malloc(sizeof(Config));
    if (!config) {
        printf("Error: Failed to allocate memory for Config.\n");
        exit(1);
    }

    memory = malloc(sizeof(Memory));
    if (!memory) {
        printf("Error: Failed to allocate memory for Memory.\n");
        free(config);
        exit(1);
    }
    readConfigFile(config);
    cache = initalizeCache(config);
    initializeMemory(memory, config);


}

//this is only supposed to handle a read operation. In case of a hit, its quite simple.
//In case of a cold miss, the cache line is loaded with the data  blcok fetched from memory.
//In case of a conflict miss, suitable line based on the eviction policy is removed followed by fetching of the data block from the memory.
int handleRead(Cache *cache, Memory *memory, unsigned int addr, ReplacementPolicy policy){
    //user enters an address
    //look for this in the cache first
    //if its a hit
        //return the value and exit the method
    //if its a miss
        // read that address from memory
        // evict a line from cache based on replacement policy, 
        // fetch that block from memory to load it in the cache
        //exit method
    //if its a conflict miss
        //read from memory again
        // fetch block from memory, evict a line from cache based on replacement policy, load into cache
        // exit method
    
    //look in cache. The byte comes back through a uint8_t out-parameter and is
    //returned as an int, so a legitimate 0xFF can never look like an error code.
    uint8_t cached_byte;
    int hit;
    hit = checkCache(cache, addr, &cached_byte);

    switch(hit){
        case 1:
        printf("Cache hit!\n");
            printf("%c\n", cached_byte);
            return cached_byte; //successfull cache access, didnt have to bother looking into memory
        
        case 0:
            printf("Cache miss! Fetching data from memory...\n");
            uint8_t *block_data = NULL;
            int err = fetchBlockFromMemory(memory, addr, &block_data); // Fetch block from memory
            if(err != 0){
                printf("Error: Failed to fetch block from memory (error code %d).\n", err);
                return err;
            }

            if (!block_data) {
                printf("Error: Failed to fetch block from memory.\n");
                return -1;
            }

            // Handle cache replacement for the fetched block
            Line *line_to_replace = handleLineReplacement(cache, addr, policy);

            if (!line_to_replace) {
                printf("Error: Cache replacement failed.\n");
                free(block_data); // Free the block data fetched from memory
                return 1;
            }

            // Update the cache line with the fetched block
            int tag_bits = getTagBits(addr, cache->num_sets);

            updateCache(line_to_replace, tag_bits, block_data);
            // Get the data at the specific block offset within the fetched block
            int block_offset = getBlockOffset(addr);
            printf("Block offset: %d\n", block_offset);
            printf("Data at address 0x%X (loaded from memory): %d\n", addr, block_data[block_offset]);

            int fetched_byte = block_data[block_offset];
            free(block_data); // Free the block data
            return fetched_byte;

        default: // Error state
            printf("Error: Invalid cache access.\n");
            return -1;

    }
}

void handleWrite(Cache *cache, Memory *memory, unsigned int addr, uint8_t value) {
    int hit = checkCache(cache, addr, NULL);  //check if address exists in cache

    //write-through: every write reaches main memory, and it goes first so that an
    //address main memory rejects cannot leave a cache line holding a byte that
    //was never stored. Updating the line first would let the two disagree.
    if (writeToMemory(memory, addr, value) != 1) {
        return;
    }

    //no-write-allocate: the cache is touched only when the address is already
    //resident. A miss does not pull the block in.
    if (hit == 1) {
        printf("Cache hit! Writing '%c' to cache at address 0x%X\n", value, addr);

        // Update cache
        int set_index = getSetIndex(addr, cache->num_sets);
        int tag_bits = getTagBits(addr, cache->num_sets);
        int block_offset = getBlockOffset(addr);
        Set *cur_set = &cache->cache_sets[set_index];

        for (int i = 0; i < cur_set->lines_per_set; i++) {
            Line *line = &cur_set->cache_lines[i];
            if (line->valid_bit && line->tag == tag_bits) {
                line->block[block_offset] = value; 
                line->last_access_time = global_time++;
                printf("data written to cache at address 0x%X\n", addr);
                break;
            }
        }
    } else {
        printf("cache miss, wrote directly to memory at 0x%X\n", addr);
    }
}


/**
 * @brief Discards the remainder of the current line of input.
 *
 * @return int Returns:
 *              - 1 if the line was discarded and more input may follow
 *              - 0 if the input stream ended while discarding
 */
int discardInputLine(){
    int c;
    while((c = getchar()) != '\n' && c != EOF);
    return c != EOF;
}

/**
 * @brief Reads a single value from stdin, rejecting anything that doesn't match.
 *
 * @param format the scanf conversion specifier to apply
 * @param out where the converted value is stored
 * @return int Returns:
 *              - 1 on a successful read
 *              - 0 if the input didn't match the format (the bad line is discarded)
 *              - -1 if the input stream ended
 *
 * Without this, a failed scanf() would leave the offending characters in the
 * buffer and the value unset, so the menu loop would spin on them forever.
 */
int readValue(const char *format, void *out){
    int matched = scanf(format, out);

    if(matched == EOF){
        return -1;
    }
    if(matched != 1){
        //drop the unreadable input so the next prompt starts on a fresh line
        return discardInputLine() ? 0 : -1;
    }
    return 1;
}

/**
 * @brief Reports that the input stream ended before the user asked to exit.
 */
void reportEndOfInput(){
    printf("\nEnd of input. Exiting Cache Simulator...\n");
}

int run(){
    //general info
    printf("\n--- Cache Simulator ---\n");
    printf("1. Read from Address\n");
    printf("2. Write to Address\n");
    printf("3. Display Cache\n");
    printf("4. Exit\n");

    while(1){
        //an unreadable choice stays 0, which the switch reports as invalid
        int choice = 0; //users selection
        unsigned int addr = 0;
        char value_to_be_written = 0;
        int status;

        printf("\nEnter your choice: ");
        if(readValue("%d", &choice) < 0){
            reportEndOfInput();
            return 0;
        }

        switch(choice){
            case 1:
                printf("Enter address (hex): 0x");
                status = readValue("%x", &addr);
                if(status < 0){
                    reportEndOfInput();
                    return 0;
                }
                if(status == 0){
                    printf("Invalid address. Try again\n");
                    break;
                }

                handleRead(cache, memory, addr, config->replacement_policy);
                break;
            
            case 2:
                printf("Enter address (hex): 0x");
                status = readValue("%x", &addr);
                if(status < 0){
                    reportEndOfInput();
                    return 0;
                }
                if(status == 0){
                    printf("Invalid address. Try again\n");
                    break;
                }

                printf("Enter value (char): ");
                status = readValue(" %c", &value_to_be_written);
                if(status < 0){
                    reportEndOfInput();
                    return 0;
                }
                if(status == 0){
                    printf("Invalid value. Try again\n");
                    break;
                }

                handleWrite(cache, memory, addr, value_to_be_written);

                break;
            
            case 3:
                displayCache(cache);
                break;
            
            case 4:
                printf("Exiting Cache Simulator...\n");
                return 0;
            
            default:
                printf("Invalid choice. Try again\n");
                break;

        }
    }

    //unreachable: every exit from the loop above returns directly. The teardown
    //lives in main(), which is what called init() to allocate these.
    return 0;
}

int main(){
    srand(time(NULL));
    
    init();

    run();

    //init() allocated these, so main() releases them. run() cannot: each of its
    //exits is a return from inside the menu loop.
    freeCache(cache);    //frees the lines, the sets array, and the Cache struct
    freeMemory(memory);  //frees the pages and the page table, but not the struct
    free(memory);        //...which is heap allocated here, so it is freed too
    free(config);
    return 0;
}