#include "../include/memory.h"
#include <stdlib.h>

int initialize_memory(memory_t *memory, const config_t *config) {
    // A partial final block could not be fetched safely.
    if (config->block_size <= 0 || config->main_memory_size <= 0 ||
        config->main_memory_size % config->block_size != 0) {
        return -1;
    }

    memory->total_size = config->main_memory_size;
    memory->block_size = config->block_size;
    memory->page_size = 256; // fixed page size
    // This overflow-safe expression rounds up to a whole page.
    memory->num_pages =
        memory->total_size / memory->page_size + (memory->total_size % memory->page_size != 0);

    memory->page_table = (uint8_t **)calloc(memory->num_pages, sizeof(uint8_t *));
    if (!memory->page_table) {
        return -1;
    }

    return 0;
}

int allocate_page(memory_t *memory, int page_index) {
    if (!memory || !memory->page_table) {
        return -1;
    }

    if (page_index >= memory->num_pages || page_index < 0) {
        return -2;
    }

    if (memory->page_table[page_index] == NULL) {
        memory->page_table[page_index] = (uint8_t *)malloc(memory->page_size * sizeof(uint8_t));
        if (!memory->page_table[page_index]) {
            return -3;
        }

        // Printable data keeps interactive cache dumps readable.
        for (int i = 0; i < memory->page_size; i++) {
            memory->page_table[page_index][i] =
                32 + (rand() % (126 - 32 + 1)); // ASCII range [32, 126]
        }
    }
    return 1;
}

int read_from_memory(memory_t *memory, int address) {
    if (!memory || !memory->page_table) {
        return -1;
    }

    int page_idx = address / memory->page_size;
    int offset = address % memory->page_size;

    if (address >= memory->total_size || address < 0) {
        return -1;
    }

    if (allocate_page(memory, page_idx) != 1) {
        return -1;
    }

    return memory->page_table[page_idx][offset];
}

int write_to_memory(memory_t *memory, int address, uint8_t value) {
    if (!memory || !memory->page_table) {
        return 0;
    }

    int page_idx = address / memory->page_size;
    int offset = address % memory->page_size;

    if (address >= memory->total_size || address < 0) {
        return 0;
    }
    // Initialize the whole page so neighboring bytes always have defined values.
    if (allocate_page(memory, page_idx) != 1) {
        return 0;
    }
    memory->page_table[page_idx][offset] = value;
    return 1;
}

int write_block_to_memory(memory_t *memory, unsigned int block_address, const uint8_t *block_data) {
    if (!memory || !memory->page_table || !block_data || memory->block_size <= 0) {
        return 0;
    }
    if (block_address % (unsigned int)memory->block_size != 0 ||
        block_address >= (unsigned int)memory->total_size ||
        (unsigned int)memory->block_size > (unsigned int)memory->total_size - block_address) {
        return 0;
    }

    // Allocate first so failure cannot leave a partially written dirty block.
    unsigned int last = block_address + (unsigned int)memory->block_size - 1u;
    int first_page = (int)block_address / memory->page_size;
    int last_page = (int)last / memory->page_size;
    for (int page = first_page; page <= last_page; page++) {
        if (allocate_page(memory, page) != 1) {
            return 0;
        }
    }

    for (int i = 0; i < memory->block_size; i++) {
        unsigned int address = block_address + (unsigned int)i;
        int page = (int)address / memory->page_size;
        int offset = (int)address % memory->page_size;
        memory->page_table[page][offset] = block_data[i];
    }
    return 1;
}

int fetch_block_from_memory(memory_t *memory, unsigned int addr, uint8_t **block_data) {
    if (!memory || !memory->page_table) {
        return -1;
    }

    if (!block_data) {
        return -1;
    }

    if (addr >= (unsigned int)memory->total_size) {
        return -1;
    }

    unsigned int offset_mask = (unsigned int)memory->block_size - 1u;
    int block_start_addr = (int)(addr & ~offset_mask);
    *block_data = malloc((size_t)memory->block_size);
    if (!*block_data) {
        return -3;
    }

    for (int i = 0; i < memory->block_size; i++) {
        int value = read_from_memory(memory, block_start_addr + i);
        if (value < 0) {
            free(*block_data);
            *block_data = NULL;
            return value;
        }
        (*block_data)[i] = (uint8_t)value;
    }
    return 0;
}

void free_memory(memory_t *memory) {
    if (!memory || !memory->page_table) {
        return;
    }

    for (int i = 0; i < memory->num_pages; i++) {
        if (memory->page_table[i] != NULL) {
            free(memory->page_table[i]);
        }
    }
    free(memory->page_table);
    memory->page_table = NULL;
}
