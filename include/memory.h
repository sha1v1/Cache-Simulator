#ifndef MEMORY_H
#define MEMORY_H
#include "config.h"
#include <stdint.h>

typedef struct {
    uint8_t **page_table;
    int total_size;
    int page_size;
    int block_size;
    int num_pages;
} memory_t;

int initialize_memory(memory_t *memory, const config_t *config);
int allocate_page(memory_t *memory, int page_index);
int read_from_memory(memory_t *memory, int address);
int write_to_memory(memory_t *memory, int address, uint8_t value);
int write_block_to_memory(memory_t *memory, unsigned int block_address, const uint8_t *block_data);
int fetch_block_from_memory(memory_t *memory, unsigned int addr, uint8_t **block_data);
void free_memory(memory_t *memory);

#endif
