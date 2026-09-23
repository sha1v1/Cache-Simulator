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
    TEST_ASSERT_EQUAL(4, cache->num_sets);
    TEST_ASSERT_EQUAL(2, cache->lines_per_set);  // Two lines per set
    TEST_ASSERT_NOT_NULL(cache->cache_sets);

    char zero_block[BLOCK_SIZE];
    memset(zero_block, 0, BLOCK_SIZE);

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 2; j++) {
            Line line = cache->cache_sets[i].cache_lines[j];
            TEST_ASSERT_FALSE(line.valid_bit);
            TEST_ASSERT_EQUAL(0, line.tag);
            TEST_ASSERT_EQUAL_MEMORY(zero_block, line.block, BLOCK_SIZE);
        }
    }
}

void test_cacheAccess(void) {
    unsigned int address = 0x1234;
    char out_data;

    // Check miss
    int result = checkCache(cache, address, &out_data);
    TEST_ASSERT_EQUAL(0, result);

    // Simulate fetching data and updating cache
    char block_data[BLOCK_SIZE];
    makeBlock(block_data, "BlockData");
    updateCache(&cache->cache_sets[getSetIndex(address, 4)].cache_lines[0],
                getTagBits(address, 4),
                block_data);

    // Check hit
    result = checkCache(cache, address, &out_data);
    TEST_ASSERT_EQUAL(1, result);

}


void test_update_cache(void) {
    int set_index = 1;
    int line_index = 0;
    int tag_bits = 6;
    char block_data[BLOCK_SIZE];
    makeBlock(block_data, "NewData");

    Line *line = &cache->cache_sets[set_index].cache_lines[line_index];
    updateCache(line, tag_bits, block_data);

    TEST_ASSERT_TRUE(line->valid_bit);
    TEST_ASSERT_EQUAL(tag_bits, line->tag);
    TEST_ASSERT_EQUAL_MEMORY(block_data, line->block, BLOCK_SIZE);
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
    Line *replaced_line = handleLineReplacement(cache, address3, "RANDOM");
    updateCache(replaced_line, getTagBits(address3, 4), block3);

    // Verify Random replacement
    // Ensure that the tag of the replaced line matches one of the first two tags
    unsigned int tag = getTagBits(address3, 4);
    TEST_ASSERT_TRUE(cache->cache_sets[1].cache_lines[0].tag == tag || cache->cache_sets[1].cache_lines[1].tag == tag);


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

    Line *replaced_line = handleLineReplacement(cache, address3, "LRU");
    updateCache(replaced_line, getTagBits(address3, 4), block3);
    
    // Verify LRU replacement, line 0 should be replaced
    unsigned int tag = getTagBits(address3, 4);
    TEST_ASSERT_EQUAL(tag, cache->cache_sets[1].cache_lines[0].tag); // Line 0 should be replaced
    
}



void test_invalid_cache_access(void) {
    Cache *invalid_cache = NULL;
    unsigned int addr = 50;

    int result = checkCache(invalid_cache, addr, NULL);
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
