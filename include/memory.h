#ifndef MEMORY_H
#define MEMORY_H
#include "config.h"

//Bytes per cache block: the unit memory and cache exchange, and the size of
//the data array in a cache Line. Defined here because fetchBlockFromMemory
//produces blocks of exactly this size; cache.h includes this header.
//Must be a power of two, since the block offset is masked out of an address.
#define BLOCK_SIZE        32
#define BLOCK_OFFSET_BITS 5                  //log2(BLOCK_SIZE)
#define BLOCK_MASK        (BLOCK_SIZE - 1)   //keeps just the block offset bits

typedef struct {
    char **pageTable;  // Array of page pointers
    int totalSize;     // Total memory size in bytes
    int pageSize;      // Size of each page in bytes
    int numPages;      // Total number of pages
} Memory;

void initializeMemory(Memory *memory, Config *config);
int allocatePage(Memory *memory, int pageIndex);
int readFromMemory(Memory *memory, int address);
int writeToMemory(Memory *memory, int address, char value);
int fetchBlockFromMemory(Memory *memory, unsigned int addr, char **blockData);
void freeMemory(Memory *memory);

#endif