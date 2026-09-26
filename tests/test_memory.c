#include "../include/memory.h"
#include "../include/config.h"
#include "../src/unity/unity.h"
#include "../include/memory.h"
#include <stdio.h>

memory_t memory;
config_t config;

void setUp(void){
    config.main_memory_size = 1024;
    config.block_size = DEFAULT_BLOCK_SIZE;
    initialize_memory(&memory, &config);
}

void tearDown(void){
    free_memory(&memory);
}

void test_initialize_memory(){
    TEST_ASSERT_NOT_NULL(memory.page_table);
    TEST_ASSERT_EQUAL(4, memory.num_pages);
    TEST_ASSERT_EQUAL(256, memory.page_size);
    TEST_ASSERT_EQUAL(1024, memory.total_size);


}

void test_read_from_memory(void){
    int value = read_from_memory(&memory, 500);
    TEST_ASSERT(value >= 32 && value <= 126);
}

void test_write_to_memory(void){
    write_to_memory(&memory, 500, 42);
    int value = read_from_memory(&memory, 500);
    TEST_ASSERT_EQUAL(42, value);

}

//Writing one byte has to bring the whole page into existence populated, the
//same as a read does. Otherwise the other 255 bytes on that page hold whatever
//malloc returned, and nothing in the program ever decided their values.
void test_write_then_read_untouched_neighbour(void){
    write_to_memory(&memory, 500, 42);            //first touch of page 1
    int value = read_from_memory(&memory, 501);   //a byte nobody ever wrote
    TEST_ASSERT(value >= 32 && value <= 126);
}

void test_out_of_bounds_access(void) {
    int read_value = read_from_memory(&memory, 2000);  // Out of bounds
    TEST_ASSERT_EQUAL(-1, read_value);               // Should return -1

    int write_success = write_to_memory(&memory, 2000, 42);
    TEST_ASSERT_EQUAL(0, write_success);             // Should fail
}
//check for
// 1. failed to initialize memory
// 2. trying to allocated invalid page index (out of bounds)
// 3. read from memory with out of bounds address/ uninitialized memory
// 4. write to memory to an out of bounds address or uninitialized memory

void test_uninitialized_memory(void){
    //this is zero initialialization. This ensures page_table = NULL
    //and other fields are zero.
    //leaving "invalid_memory" in an uninitialized state might cause page_table to point to garbalge values.
    memory_t invalid_memory = {0};
    int val = read_from_memory(&invalid_memory, 101);
    TEST_ASSERT_EQUAL(-1, val);

    TEST_ASSERT_EQUAL(write_to_memory(&invalid_memory, 100, 10), 0);
}

void test_allocate_invalid_page_index(void){
    int ret = allocate_page(&memory, 7);
    TEST_ASSERT_EQUAL(-2, ret);
}

void test_allocate_page_invalid_memory(void){
    memory_t invalid_memory = {0};
    int ret = allocate_page(&invalid_memory, 1);
    TEST_ASSERT_EQUAL(-1, ret);
}

void test_read_from_memory_uninitialized(void){
    memory_t invalid_memory = {0};
    int ret1 = read_from_memory(NULL, 2);
    int ret2 = read_from_memory(&invalid_memory, 2);
    TEST_ASSERT_EQUAL(-1, ret1);
    TEST_ASSERT_EQUAL(-1, ret2);
}

void test_read_from_memory_invalid_address(void){
    int ret1 = read_from_memory(&memory, 1024);
    int ret2 = read_from_memory(&memory, -2);
    TEST_ASSERT_EQUAL(-1, ret1);
    TEST_ASSERT_EQUAL(-1, ret2);
}

void test_write_to_memory_invalid_address(void){
    int ret1 = write_to_memory(&memory, -1, 'a');
    int ret2 = write_to_memory(&memory, 2000, 'a');
    TEST_ASSERT_EQUAL(0, ret1);
    TEST_ASSERT_EQUAL(0, ret2);
}

void test_write_to_memory_uninitialized(void){
    memory_t invalid_memory = {0};
    int ret1 = write_to_memory(NULL, 2, 'a');
    int ret2 = write_to_memory(&invalid_memory, 2, 'a');
    TEST_ASSERT_EQUAL(0, ret1);
    TEST_ASSERT_EQUAL(0, ret2);
}

//fetch_block_from_memory must refuse an unusable memory_t rather than dereferencing
//it, and must not report success while filling the block with error codes.
void test_fetch_block_uninitialized_memory(void){
    memory_t invalid_memory = {0};
    uint8_t *block = NULL;

    TEST_ASSERT_EQUAL(-1, fetch_block_from_memory(NULL, 0, &block));
    TEST_ASSERT_EQUAL(-1, fetch_block_from_memory(&invalid_memory, 0, &block));
    TEST_ASSERT_EQUAL(-1, fetch_block_from_memory(&memory, 0, NULL));
    TEST_ASSERT_NULL(block);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_initialize_memory);
    RUN_TEST(test_read_from_memory);
    RUN_TEST(test_write_to_memory);
    RUN_TEST(test_write_then_read_untouched_neighbour);
    RUN_TEST(test_out_of_bounds_access);
    RUN_TEST(test_uninitialized_memory);
    RUN_TEST(test_allocate_invalid_page_index);
    RUN_TEST(test_allocate_page_invalid_memory);
    RUN_TEST(test_read_from_memory_uninitialized);
    RUN_TEST(test_read_from_memory_invalid_address);
    RUN_TEST(test_write_to_memory_invalid_address);
    RUN_TEST(test_write_to_memory_uninitialized);
    RUN_TEST(test_fetch_block_uninitialized_memory);
    return UNITY_END();
}
