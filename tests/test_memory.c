#include "../include/memory.h"
#include "../include/config.h"
#include "../src/unity/unity.h"
#include "../include/memory.h"
#include <stdio.h>

Memory memory;
Config config;

void setUp(void){
    config.mainMemorySize = 1024;
    initializeMemory(&memory, &config);
}

void tearDown(void){
    freeMemory(&memory);
}

void test_initialize_memory(){
    TEST_ASSERT_NOT_NULL(memory.pageTable);
    TEST_ASSERT_EQUAL(4, memory.numPages);
    TEST_ASSERT_EQUAL(256, memory.pageSize);
    TEST_ASSERT_EQUAL(1024, memory.totalSize);


}

void test_read_from_memory(void){
    char value = readFromMemory(&memory, 500);
    TEST_ASSERT(value >= 32 && value <= 126);
}

void test_write_to_memory(void){
    writeToMemory(&memory, 500, 42);
    char value = readFromMemory(&memory, 500);
    TEST_ASSERT_EQUAL(42, value);

}

//Writing one byte has to bring the whole page into existence populated, the
//same as a read does. Otherwise the other 255 bytes on that page hold whatever
//malloc returned, and nothing in the program ever decided their values.
void test_write_then_read_untouched_neighbour(void){
    writeToMemory(&memory, 500, 42);            //first touch of page 1
    int value = readFromMemory(&memory, 501);   //a byte nobody ever wrote
    TEST_ASSERT(value >= 32 && value <= 126);
}

void test_out_of_bounds_access(void) {
    char readValue = readFromMemory(&memory, 2000);  // Out of bounds
    TEST_ASSERT_EQUAL(-1, readValue);               // Should return -1

    int writeSuccess = writeToMemory(&memory, 2000, 42);
    TEST_ASSERT_EQUAL(0, writeSuccess);             // Should fail
}
//check for
// 1. failed to initialize memory
// 2. trying to allocated invalid page index (out of bounds)
// 3. read from memory with out of bounds address/ uninitialized memory
// 4. write to memory to an out of bounds address or uninitialized memory

void test_uninitialized_memory(void){
    //this is zero initialialization. This ensures pageTable = NULL
    //and other fields are zero.
    //leaving "invalidMemory" in an uninitialized state might cause pageTable to point to garbalge values.
    Memory invalidMemory = {0};
    int val = readFromMemory(&invalidMemory, 101);
    TEST_ASSERT_EQUAL(-1, val);

    TEST_ASSERT_EQUAL(writeToMemory(&invalidMemory, 100, 10), 0);
}

void test_allocate_invalid_page_index(void){
    int ret = allocatePage(&memory, 7);
    TEST_ASSERT_EQUAL(-2, ret);
}

void test_allocate_page_invalid_memory(void){
    Memory invalidMemory = {0};
    int ret = allocatePage(&invalidMemory, 1);
    TEST_ASSERT_EQUAL(-1, ret);
}

void test_read_from_memory_uninitialized(void){
    Memory invalidMemory = {0};
    int ret1 = readFromMemory(NULL, 2);
    int ret2 = readFromMemory(&invalidMemory, 2);
    TEST_ASSERT_EQUAL(-1, ret1);
    TEST_ASSERT_EQUAL(-1, ret2);
}

void test_read_from_memory_invalid_address(void){
    int ret1 = readFromMemory(&memory, 1024);
    int ret2 = readFromMemory(&memory, -2);
    TEST_ASSERT_EQUAL(-1, ret1);
    TEST_ASSERT_EQUAL(-1, ret2);
}

void test_write_to_memory_invalid_address(void){
    int ret1 = writeToMemory(&memory, -1, 'a');
    int ret2 = writeToMemory(&memory, 2000, 'a');
    TEST_ASSERT_EQUAL(0, ret1);
    TEST_ASSERT_EQUAL(0, ret2);
}

void test_write_to_memory_uninitialized(void){
    Memory invalidMemory = {0};
    int ret1 = writeToMemory(NULL, 2, 'a');
    int ret2 = writeToMemory(&invalidMemory, 2, 'a');
    TEST_ASSERT_EQUAL(0, ret1);
    TEST_ASSERT_EQUAL(0, ret2);
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
    return UNITY_END();
}
