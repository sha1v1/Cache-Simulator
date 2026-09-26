#ifndef MEMORY_H
#define MEMORY_H
#include "config.h"
#include <stdint.h>

//Bytes per cache block: the unit memory and cache exchange, and the size of
//the data array in a cache line. Defined here because fetch_block_from_memory
//produces blocks of exactly this size; cache.h includes this header.
//Must be a power of two, since the block offset is masked out of an address.
#define BLOCK_SIZE        32
#define BLOCK_OFFSET_BITS 5                  //log2(BLOCK_SIZE)
#define BLOCK_MASK        (BLOCK_SIZE - 1)   //keeps just the block offset bits

typedef struct {
    uint8_t **page_table;  //Array of page pointers. Bytes, not text: uint8_t
                           //is unsigned everywhere, so a byte read back through
                           //an int lands in 0..255 and can never be mistaken
                           //for a negative status code.
    int total_size;     // Total memory size in bytes
    int page_size;      // Size of each page in bytes
    int num_pages;      // Total number of pages
} memory_t;

//Returns 0 on success, or -1 if the size is unusable or allocation failed.
int initialize_memory(memory_t *memory, config_t *config);
int allocate_page(memory_t *memory, int page_index);
int read_from_memory(memory_t *memory, int address);
int write_to_memory(memory_t *memory, int address, uint8_t value);
int fetch_block_from_memory(memory_t *memory, unsigned int addr, uint8_t **block_data);
void free_memory(memory_t *memory);

#endif