#include "../src/unity/unity.h"
#include "../include/cache.h"
#include <string.h>
#include <stdlib.h>

Cache *cache;

/* updateCache copies a whole BLOCK_SIZE block, so tests must supply one.
   Build it from a short label and leave the remaining bytes zero. */
static void makeBlock(char out[BLOCK_SIZE], const char *label){
    memset(out, 0, BLOCK_SIZE);
    memcpy(out, label, strlen(label));
}

void setUp(void) {
    // Initialize a cache with 4 sets and 2 lines per set
    Config config = {4, 1024, 2}; 
    cache = initalizeCache(&config);
}

void tearDown(void) {
    freeCache(cache);
}

void test_initialize_cache(void) {
    TEST_ASSERT_NOT_NULL(cache);
    TEST_ASSERT_EQUAL(4, cache->numSets);
    TEST_ASSERT_EQUAL(2, cache->linesPerSet);  // Two lines per set
    TEST_ASSERT_NOT_NULL(cache->cacheSets);

    char zeroBlock[BLOCK_SIZE];
    memset(zeroBlock, 0, BLOCK_SIZE);

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 2; j++) {
            Line line = cache->cacheSets[i].cacheLines[j];
            TEST_ASSERT_FALSE(line.validBit);
            TEST_ASSERT_EQUAL(0, line.tag);
            TEST_ASSERT_EQUAL_MEMORY(zeroBlock, line.block, BLOCK_SIZE);
        }
    }
}

void test_cacheAccess(void) {
    unsigned int address = 0x1234;
    char outData;

    // Check miss
    int result = checkCache(cache, address, &outData);
    TEST_ASSERT_EQUAL(0, result);

    // Simulate fetching data and updating cache
    char blockData[BLOCK_SIZE];
    makeBlock(blockData, "BlockData");
    updateCache(&cache->cacheSets[getSetIndex(address, 4)].cacheLines[0],
                getTagBits(address, 4),
                blockData);

    // Check hit
    result = checkCache(cache, address, &outData);
    TEST_ASSERT_EQUAL(1, result);

}


void test_update_cache(void) {
    int setIndex = 1;
    int lineIndex = 0;
    int tagBits = 6;
    char blockData[BLOCK_SIZE];
    makeBlock(blockData, "NewData");

    Line *line = &cache->cacheSets[setIndex].cacheLines[lineIndex];
    updateCache(line, tagBits, blockData);

    TEST_ASSERT_TRUE(line->validBit);
    TEST_ASSERT_EQUAL(tagBits, line->tag);
    TEST_ASSERT_EQUAL_MEMORY(blockData, line->block, BLOCK_SIZE);
}

void test_RandomReplacement(void){
    unsigned int address1 = 0x1234; // Maps to set 1, line 0
    unsigned int address2 = 0x00a4; // Maps to set 1, line 1
    unsigned int address3 = 0x0234; // Maps to set 1, should replace a random line

    char block1[BLOCK_SIZE], block2[BLOCK_SIZE], block3[BLOCK_SIZE];
    makeBlock(block1, "Block1");
    makeBlock(block2, "Block2");
    makeBlock(block3, "Block3");

    // Access first two addresses (fills both lines)
    checkCache(cache, address1, NULL);
    updateCache(handleLineReplacement(cache, address1, "RANDOM"), getTagBits(address1, 4), block1);

    checkCache(cache, address2, NULL);
    updateCache(handleLineReplacement(cache, address2, "RANDOM"), getTagBits(address2, 4), block2);

    // Access a new address to trigger replacement
    Line *replacedLine = handleLineReplacement(cache, address3, "RANDOM");
    updateCache(replacedLine, getTagBits(address3, 4), block3);

    // Verify Random replacement
    // Ensure that the tag of the replaced line matches one of the first two tags
    unsigned int tag = getTagBits(address3, 4);
    TEST_ASSERT_TRUE(cache->cacheSets[1].cacheLines[0].tag == tag || cache->cacheSets[1].cacheLines[1].tag == tag);


}


void test_LRUReplacement(void) {
    unsigned int address1 = 0x1234; // Maps to set 0, line 0
    unsigned int address2 = 0x00a4; // Maps to set 0, line 1
    unsigned int address3 = 0x0234; // Maps to set 0, should replace line 0 (LRU)

    char block1[BLOCK_SIZE], block2[BLOCK_SIZE], block3[BLOCK_SIZE];
    makeBlock(block1, "Block1");
    makeBlock(block2, "Block2");
    makeBlock(block3, "Block3");

    // Access first two addresses (fills both lines)
    checkCache(cache, address1, NULL);
    updateCache(handleLineReplacement(cache, address1, "LRU"), getTagBits(address1, 4), block1);

    checkCache(cache, address2, NULL);
    updateCache(handleLineReplacement(cache, address2, "LRU"), getTagBits(address2, 4), block2);

    //access line 1 again to make line 0 the least recently use line
    checkCache(cache, address2, NULL);

    // Access a new address to trigger replacement
    checkCache(cache, address3, NULL);

    Line *replacedLine = handleLineReplacement(cache, address3, "LRU");
    updateCache(replacedLine, getTagBits(address3, 4), block3);
    
    // Verify LRU replacement, line 0 should be replaced
    unsigned int tag = getTagBits(address3, 4);
    TEST_ASSERT_EQUAL(tag, cache->cacheSets[1].cacheLines[0].tag); // Line 0 should be replaced
    
}



void test_invalid_cache_access(void) {
    Cache *invalidCache = NULL;
    unsigned int addr = 50;

    int result = checkCache(invalidCache, addr, NULL);
    TEST_ASSERT_EQUAL(-1, result);  // Expect cache access to fail gracefully
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_initialize_cache);
    RUN_TEST(test_cacheAccess);
    RUN_TEST(test_update_cache);
    RUN_TEST(test_RandomReplacement);
    RUN_TEST(test_LRUReplacement);
    RUN_TEST(test_invalid_cache_access);

    return UNITY_END();
}
