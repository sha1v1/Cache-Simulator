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
char handleRead(Cache *cache, Memory *memory, unsigned int addr, const char policy[10]){
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
    
    //look in cache
    char fetchedData;
    int hit;
    hit = checkCache(cache, addr, &fetchedData);

    switch(hit){
        case 1:
        printf("Cache hit!\n");
            printf("%c\n", fetchedData);
            return fetchedData; //successfull cache access, didnt have to bother looking into memory
        
        case 0:
            printf("Cache miss! Fetching data from memory...\n");
            char *blockData = NULL;
            int err = fetchBlockFromMemory(memory, addr, &blockData); // Fetch block from memory
            if(err != 0){
                printf("Error: Failed to fetch block from memory (error code %d).\n", err);
                return err;
            }
            fetchedData = (char)readFromMemory(memory, addr);

            if (!blockData) {
                printf("Error: Failed to fetch block from memory.\n");
                return -1;
            }

            // Handle cache replacement for the fetched block
            Line *lineToReplace = handleLineReplacement(cache, addr, policy);

            if (!lineToReplace) {
                printf("Error: Cache replacement failed.\n");
                free(blockData); // Free the block data fetched from memory
                return 1;
            }

            // Update the cache line with the fetched block
            int tagBits = getTagBits(addr, cache->numSets);

            updateCache(lineToReplace, tagBits, blockData);
            // Get the data at the specific block offset within the fetched block
            int blockOffset = getBlockOffset(addr);
            printf("Block offset: %d\n", blockOffset);
            printf("Data at address 0x%X (loaded from memory): %d\n", addr, blockData[blockOffset]);

            free(blockData); // Free the block data
            return fetchedData;

        default: // Error state
            printf("Error: Invalid cache access.\n");
            return -1;

    }
}

void handleWrite(Cache *cache, Memory *memory, unsigned int addr, char value) {
    int hit = checkCache(cache, addr, NULL);  //check if address exists in cache

    //write-around so only update cache if there is a hit
    if (hit == 1) {
        printf("Cache hit! Writing '%c' to cache at address 0x%X\n", value, addr);

        // Update cache
        int setIndex = getSetIndex(addr, cache->numSets);
        int tagBits = getTagBits(addr, cache->numSets);
        int blockOffset = getBlockOffset(addr);
        Set *curSet = &cache->cacheSets[setIndex];

        for (int i = 0; i < curSet->linesPerSet; i++) {
            Line *line = &curSet->cacheLines[i];
            if (line->validBit && line->tag == tagBits) {
                line->block[blockOffset] = value; 
                line->lastAccessTime = globalTime++;
                printf("data written to cache at address 0x%X\n", addr);
                break;
            }
        }
    } else {
        printf("cache miss, writing directly to memory at 0x%X\n", addr);
    }

    //write through so always update memory (write-around)
    writeToMemory(memory, addr, value);
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
        char valueToBeWritten = 0;
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

                handleRead(cache, memory, addr, config->replacementPolicy);
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
                status = readValue(" %c", &valueToBeWritten);
                if(status < 0){
                    reportEndOfInput();
                    return 0;
                }
                if(status == 0){
                    printf("Invalid value. Try again\n");
                    break;
                }

                handleWrite(cache, memory, addr, valueToBeWritten);

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

    freeCache(cache);
    freeMemory(memory);
    free(config);
    return 0;
}

int main(){
    srand(time(NULL));
    
    init();

    run();
    return 0;
}