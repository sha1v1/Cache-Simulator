#ifndef  CACHE_H
#define CACHE_H
#include <stdbool.h>
#include "memory.h"

extern unsigned int globalTime;

typedef struct {
    bool validBit;
    unsigned int tag;   
    char block[BLOCK_SIZE];   //raw bytes, not a string - no terminator
    unsigned int lastAccessTime;
} Line;

typedef struct {
    Line *cacheLines; //pointer to array of lines
    int linesPerSet;
} Set;

typedef struct {
    Set* cacheSets;
    int numSets;
    int linesPerSet; //just easy to access lol
} Cache;


Cache* initalizeCache(Config *config);
void initializeSets(Set* sets, int numSets, int linesPerSet);
int getSetIndex(unsigned int addr, int numSets);
int getBlockOffset(unsigned int addr);
int getTagBits(unsigned int addr, int numSets);
int checkCache(Cache *cache, unsigned int addr, char* outData);
Line *handleLineReplacement(Cache *cache, unsigned int addr, const char *policy);
Line *randomReplacement(Set *set);
Line* leastRecentlyUsed(Set *set);
void updateCache(Line *line, int tagBits, const char *blockData);
void displayCache(Cache *c);
void freeCache(Cache *cache);

#endif


