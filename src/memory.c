#include "../include/memory.h"
#include "../include/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//initialize main memory
/**
 * @brief Initialize the Main memory Structure with user provided configuration.
 * 
 * @param memory A pointer to the Memory structure.
 * @param config A pointer to the Config structure
 * 
 * Sets the total memory size, page size, calculates number of pages required. It also allocates memory for
 * the page_table which is essentially an array of pointers to all these pages. These pointers are
 * initialized to NULL.
 */
int initializeMemory(Memory *memory, Config *config){
    //fetchBlockFromMemory aligns down to a block boundary and always reads
    //BLOCK_SIZE bytes, so a memory that doesn't end on a block boundary would
    //have its last block run past the end and store readFromMemory's error
    //returns as if they were data.
    if (config->main_memory_size <= 0 || config->main_memory_size % BLOCK_SIZE != 0) {
        return -1;
    }

    memory->total_size = config->main_memory_size;
    memory->page_size = 256; //fixed page size
    memory->num_pages = (memory->total_size + memory->page_size - 1)/memory->page_size; //round up

    //allocate memory for array of page pointers
    memory->page_table = (uint8_t **)calloc(memory->num_pages, sizeof(uint8_t *));
    if(!memory->page_table){
        return -1;
    }

    return 0;
}

/**
 * @brief Allocates (initializes) a memory page at the specified index.
 *
 * @param memory A pointer to the Memory structure.
 * @param page_index The index of the page to allocate.
 * @return int Returns:
 *              - 1 if the page was successfully allocated or already exists.
 *              - -1 if the memory is uninitialized.
 *              - -2 if the page index is out of bounds.
 *              - -3 if page allocation failed.
 *
 * Checks if the page has already been allocated. If not,
 * it dynamically allocates the page and populates it with random non-zero values.
 */
int allocatePage(Memory *memory, int page_index) {
    //check if memory has been initiliazed
    if(!memory || !memory->page_table){
        return -1;
    }

    //check if the page index lies within memory's bounds
    if(page_index >= memory->num_pages || page_index < 0){
        return -2;
    }

    //check if the page has been initiliazed
    if (memory->page_table[page_index] == NULL) {
        //allocate memory for the page
        memory->page_table[page_index] = (uint8_t *)malloc(memory->page_size * sizeof(uint8_t));
        if (!memory->page_table[page_index]) {
            return -3;
        }

        // Populate with random non-zero values
        for (int i = 0; i < memory->page_size; i++) {
            memory->page_table[page_index][i] = 32 + (rand() % (126 - 32 + 1));  // ASCII range [32, 126]
        }

    }
    return 1;
}

/**
 * @brief Given a memory address, it returns the value at the said address.
 * @param memory A pointer to the Memory struct
 * @param address The address to read from
 * 
 * cacluate the page and offset from the given address. If the page hasn't been allocated (initialized),
 * it is dynamically polulated with random values and the value at the given address is returned. 
*/
int readFromMemory(Memory *memory, int address){
    if(!memory || !memory->page_table){
        return -1;
    }
    
    int page_idx = address/memory->page_size; //page index where the address is
    int offset = address % memory->page_size; //offset within that page

    if(address >= memory->total_size|| address < 0){
        return -1;
    }

    //populate page if this is the first time an address from this page has been accessed
    if (allocatePage(memory, page_idx) != 1) {
        return -1;
    }

    //otherwise return the value at the given address
    return memory->page_table[page_idx][offset];
}

/**
 * @brief Writes a value to a given address in memory.
 * 
 * @param memory A pointer to the memory structure.
 * @param address The address to write to
 * @param value The value to write to that address
 * 
 * Calculate the page index and offset from the address. If page hasn't been allocated (initialized) yet,
 * it is loaded with random values and the memory address is then written to with the provided value.
 */
int writeToMemory(Memory *memory, int address, uint8_t value){
    if(!memory || !memory->page_table){
        return 0;
    }

    int page_idx =  address/memory->page_size;
    int offset = address % memory->page_size;
    

    if (address >= memory->total_size || address < 0) {
        return 0;
    }
    //bring the page into existence the same way a read does. Allocating it here
    //without populating it would leave every other byte on the page holding
    //whatever malloc returned, which nothing in the program ever decided.
    if (allocatePage(memory, page_idx) != 1) {
        return 0;
    }
    memory->page_table[page_idx][offset] = value;
    return 1;
}

//since I am calling malloc inside, the caller will have to free the memory.
/**
 * @brief fetches a block of data (cache line) from memory.
 * 
 * @param memory A pointer to the Memory structure
 * @param addr The address that triggered the fetching of the data block
 * @returns block_data: An array of exactly BLOCK_SIZE raw bytes. It is NOT
 *          NUL-terminated - every byte is data, so callers must use
 *          BLOCK_SIZE rather than string functions.
 * 
 * Figure out the starting address of the block from the given address and fetch 32 bytes of data
 * from the computed address. In case the page hasn't been allocated yet, initialize it first.
 */
int fetchBlockFromMemory(Memory *memory, unsigned int addr, uint8_t** block_data) {
    //guard before dereferencing, as every other function in this file does.
    //Without this a half-built Memory (size set, page_table still NULL) passes
    //the bounds check and the loop below fills the block with readFromMemory's
    //error returns, reporting success while handing back 32 bytes of nothing.
    if(!memory || !memory->page_table){
        return -1;
    }

    if(!block_data){
        return -1;
    }

    //total_size is validated positive in initializeMemory, so the cast is safe
    //and keeps this from being a signed/unsigned comparison
    if(addr >= (unsigned int)memory->total_size){
        return -1;
    }

    int block_start_addr = addr & ~BLOCK_MASK;  // Align to block start
    *block_data = malloc(BLOCK_SIZE * sizeof(char));
    if (!*block_data) {
        return -3; // Erroneous fetch attempt: malloc failed
    }

    //every one of the BLOCK_SIZE bytes is data. No terminator is written:
    //offsets 0..BLOCK_SIZE-1 are all addressable, so reserving the last byte
    //for a '\0' would silently destroy the byte the caller asked for.
    for (int i = 0; i < BLOCK_SIZE; i++) {
        //readFromMemory returns a byte as 0..255 and any failure as a negative
        //value, so the two can be told apart here rather than storing an error
        //code into the block as if it were data.
        int value = readFromMemory(memory, block_start_addr + i);
        if (value < 0) {
            free(*block_data);
            *block_data = NULL;
            return value;
        }
        (*block_data)[i] = (uint8_t)value;
    }
    return 0; // for success
}
/**
 * @brief Frees all memory occupied by the Memory structure.
 *
 * @param memory A pointer to the Memory structure to free.
 *
 * This function frees all allocated pages and the page table itself.
 * After calling this, the Memory structure will be in an
 * uninitialized state.
 */
void freeMemory(Memory *memory){
    if (!memory || !memory->page_table) {
        return;
    }

    for (int i = 0; i < memory->num_pages; i++) {
        if (memory->page_table[i] != NULL) {
            free(memory->page_table[i]);  //free eacch allocated page
        }
    }
    free(memory->page_table);  //free the page table
    memory->page_table = NULL;
}

