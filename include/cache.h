#ifndef  CACHE_H
#define CACHE_H
#include <stdbool.h>
#include "memory.h"

extern unsigned int global_time;

typedef struct {
    bool valid_bit;
    unsigned int tag;   
    uint8_t block[BLOCK_SIZE];   //raw bytes, not a string - no terminator
    unsigned int last_access_time;
} Line;

typedef struct {
    Line *cache_lines; //pointer to array of lines
    int lines_per_set;
} Set;

typedef struct {
    Set* cache_sets;
    int num_sets;
    int lines_per_set; //just easy to access lol
} Cache;


Cache* initalizeCache(Config *config);
int initializeSets(Set* sets, int num_sets, int lines_per_set);
int getSetIndex(unsigned int addr, int num_sets);
int getBlockOffset(unsigned int addr);
int getTagBits(unsigned int addr, int num_sets);
int checkCache(Cache *cache, unsigned int addr, uint8_t* out_data);
Line *handleLineReplacement(Cache *cache, unsigned int addr, ReplacementPolicy policy);
Line *randomReplacement(Set *set);
Line* leastRecentlyUsed(Set *set);
void updateCache(Line *line, int tag_bits, const uint8_t *block_data);
void freeCache(Cache *cache);

#endif


