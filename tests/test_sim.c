#include "../src/unity/unity.h"
#include "../include/commands.h"
#include "../include/log.h"
#include "../include/sim.h"
#include <stdlib.h>
#include <string.h>

Simulator sim;

/* runCommandLine tokenizes its argument in place, so a literal cannot be passed
   to it. Copying into a buffer here keeps every test a one-liner. */
static CommandStatus runLine(const char *text){
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%s", text);
    return runCommandLine(&sim, buffer, false);
}

void setUp(void) {
    /* LRU rather than RANDOM: eviction has to be predictable for a test to be
       able to assert which line was displaced. */
    Config config = {4, 1024, 2, POLICY_LRU};
    /* the statistics are what these tests read, so keep the narration out of
       the test output */
    setLogLevel(LOG_QUIET);
    TEST_ASSERT_EQUAL(SIM_OK, simInit(&sim, &config));
}

void tearDown(void) {
    simFree(&sim);
}

void test_read_miss_then_hit(void) {
    AccessInfo info;

    TEST_ASSERT_EQUAL(SIM_OK, simRead(&sim, 0x100, &info));
    TEST_ASSERT_EQUAL(ACCESS_MISS, info.result);
    TEST_ASSERT_FALSE(info.evicted);

    /* the same block, one byte further along: served from the line just filled */
    TEST_ASSERT_EQUAL(SIM_OK, simRead(&sim, 0x101, &info));
    TEST_ASSERT_EQUAL(ACCESS_HIT, info.result);

    TEST_ASSERT_EQUAL(2, sim.stats.reads);
    TEST_ASSERT_EQUAL(1, sim.stats.read_hits);
    TEST_ASSERT_EQUAL(1, sim.stats.read_misses);
    TEST_ASSERT_EQUAL(0, sim.stats.evictions);
}

void test_write_through_no_write_allocate(void) {
    AccessInfo info;

    /* a write to an absent address must not pull the block into the cache */
    TEST_ASSERT_EQUAL(SIM_OK, simWrite(&sim, 0x40, 'A', &info));
    TEST_ASSERT_EQUAL(ACCESS_MISS, info.result);
    TEST_ASSERT_EQUAL(0, checkCache(sim.cache, 0x40, NULL, NULL));
    TEST_ASSERT_EQUAL(1, sim.stats.write_misses);

    /* but it must have reached memory, so the read that follows sees it */
    TEST_ASSERT_EQUAL('A', readFromMemory(&sim.memory, 0x40));

    /* now resident, so the next write is a hit and updates the line too */
    TEST_ASSERT_EQUAL(SIM_OK, simRead(&sim, 0x40, &info));
    TEST_ASSERT_EQUAL(SIM_OK, simWrite(&sim, 0x40, 'B', &info));
    TEST_ASSERT_EQUAL(ACCESS_HIT, info.result);
    TEST_ASSERT_EQUAL(1, sim.stats.write_hits);

    uint8_t cached = 0;
    TEST_ASSERT_EQUAL(1, checkCache(sim.cache, 0x40, &cached, NULL));
    TEST_ASSERT_EQUAL('B', cached);
    TEST_ASSERT_EQUAL('B', readFromMemory(&sim.memory, 0x40));
}

void test_eviction_is_counted_once_the_set_is_full(void) {
    AccessInfo info;

    /* three blocks, all mapping to set 0, in a 2-way cache: the third displaces
       the least recently used of the first two */
    TEST_ASSERT_EQUAL(SIM_OK, simRead(&sim, 0x000, &info));
    TEST_ASSERT_FALSE(info.evicted);
    TEST_ASSERT_EQUAL(SIM_OK, simRead(&sim, 0x080, &info));
    TEST_ASSERT_FALSE(info.evicted);

    TEST_ASSERT_EQUAL(SIM_OK, simRead(&sim, 0x100, &info));
    TEST_ASSERT_EQUAL(ACCESS_MISS, info.result);
    TEST_ASSERT_TRUE(info.evicted);
    TEST_ASSERT_EQUAL(1, sim.stats.evictions);

    /* LRU evicted the older of the two, so that one is gone and the other stays */
    TEST_ASSERT_EQUAL(0, checkCache(sim.cache, 0x000, NULL, NULL));
    TEST_ASSERT_EQUAL(1, checkCache(sim.cache, 0x080, NULL, NULL));
}

void test_failed_access_counts_only_as_an_error(void) {
    AccessInfo info;

    /* out of range: it is not a miss, or hits + misses would no longer account
       for exactly the accesses that completed */
    TEST_ASSERT_EQUAL(SIM_ERR_ADDRESS_RANGE, simRead(&sim, 0xFFFF, &info));
    TEST_ASSERT_EQUAL(ACCESS_ERROR, info.result);

    TEST_ASSERT_EQUAL(0, sim.stats.reads);
    TEST_ASSERT_EQUAL(0, sim.stats.read_misses);
    TEST_ASSERT_EQUAL(1, sim.stats.errors);
}

void test_reset_empties_the_cache_and_the_stats(void) {
    AccessInfo info;
    TEST_ASSERT_EQUAL(SIM_OK, simRead(&sim, 0x100, &info));

    TEST_ASSERT_EQUAL(SIM_OK, simReset(&sim));

    TEST_ASSERT_EQUAL(0, checkCache(sim.cache, 0x100, NULL, NULL));
    TEST_ASSERT_EQUAL(0, sim.stats.reads);
    TEST_ASSERT_EQUAL(0, sim.stats.read_misses);
}

void test_command_lines_drive_the_simulator(void) {
    TEST_ASSERT_EQUAL(CMD_OK, runLine("r 0x100"));
    TEST_ASSERT_EQUAL(1, sim.stats.read_misses);

    /* an address without the 0x prefix is still hex, so this is the same block */
    TEST_ASSERT_EQUAL(CMD_OK, runLine("read 101"));
    TEST_ASSERT_EQUAL(1, sim.stats.read_hits);

    /* a one-character value is that character; 0xNN is a byte */
    TEST_ASSERT_EQUAL(CMD_OK, runLine("w 0x100 7"));
    uint8_t cached = 0;
    TEST_ASSERT_EQUAL(1, checkCache(sim.cache, 0x100, &cached, NULL));
    TEST_ASSERT_EQUAL('7', cached);

    TEST_ASSERT_EQUAL(CMD_OK, runLine("write 0x100 0x41"));
    TEST_ASSERT_EQUAL(1, checkCache(sim.cache, 0x100, &cached, NULL));
    TEST_ASSERT_EQUAL(0x41, cached);

    TEST_ASSERT_EQUAL(CMD_QUIT, runLine("q"));
}

void test_command_lines_that_are_not_commands(void) {
    /* blank lines and comments are skipped rather than reported as errors */
    TEST_ASSERT_EQUAL(CMD_OK, runLine(""));
    TEST_ASSERT_EQUAL(CMD_OK, runLine("   "));
    TEST_ASSERT_EQUAL(CMD_OK, runLine("# a comment"));
    TEST_ASSERT_EQUAL(0, sim.stats.reads);

    /* trailing junk must not be read as a truncated address */
    TEST_ASSERT_EQUAL(CMD_SYNTAX_ERROR, runLine("r 0x1g"));
    TEST_ASSERT_EQUAL(CMD_SYNTAX_ERROR, runLine("r"));
    TEST_ASSERT_EQUAL(CMD_SYNTAX_ERROR, runLine("nonsense"));

    /* an ambiguous multi-character value has to say which byte it means */
    TEST_ASSERT_EQUAL(CMD_SYNTAX_ERROR, runLine("w 0x100 abc"));
    TEST_ASSERT_EQUAL(CMD_SYNTAX_ERROR, runLine("w 0x100 0x100"));

    /* a valid command whose address is out of range is understood but fails */
    TEST_ASSERT_EQUAL(CMD_FAILED, runLine("r 0xFFFF"));
}

void test_page_allocation_is_reported(void) {
    AccessInfo info;

    /* the first touch of a page is what creates it; the next touch of the same
       page is not, which is how a caller can narrate the fill without the
       engine printing it */
    TEST_ASSERT_EQUAL(SIM_OK, simRead(&sim, 0x000, &info));
    TEST_ASSERT_TRUE(info.page_allocated);

    TEST_ASSERT_EQUAL(SIM_OK, simRead(&sim, 0x0A0, &info));
    TEST_ASSERT_FALSE(info.page_allocated);

    /* page size is 256, so this is a different page */
    TEST_ASSERT_EQUAL(SIM_OK, simRead(&sim, 0x100, &info));
    TEST_ASSERT_TRUE(info.page_allocated);

    TEST_ASSERT_EQUAL(2, sim.stats.pages_allocated);
}

void test_every_status_has_a_message(void) {
    /* a caller wording an error must never be handed an empty string */
    SimStatus all[] = {SIM_OK, SIM_ERR_NUM_SETS, SIM_ERR_LINES_PER_SET,
                       SIM_ERR_MEMORY_SIZE, SIM_ERR_OUT_OF_MEMORY,
                       SIM_ERR_ADDRESS_RANGE, SIM_ERR_NOT_INITIALIZED};

    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        const char *message = simStatusMessage(all[i]);
        TEST_ASSERT_NOT_NULL(message);
        TEST_ASSERT_TRUE(message[0] != '\0');
    }
}

void test_bad_configuration_is_rejected(void) {
    Simulator bad;

    /* the status says which setting was wrong, so a caller can name it without
       the engine having to print anything */
    Config not_power_of_two = {5, 1024, 2, POLICY_LRU};
    TEST_ASSERT_EQUAL(SIM_ERR_NUM_SETS, simInit(&bad, &not_power_of_two));

    Config unaligned_memory = {4, 1000, 2, POLICY_LRU};
    TEST_ASSERT_EQUAL(SIM_ERR_MEMORY_SIZE, simInit(&bad, &unaligned_memory));

    Config no_lines = {4, 1024, 0, POLICY_LRU};
    TEST_ASSERT_EQUAL(SIM_ERR_LINES_PER_SET, simInit(&bad, &no_lines));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_read_miss_then_hit);
    RUN_TEST(test_write_through_no_write_allocate);
    RUN_TEST(test_eviction_is_counted_once_the_set_is_full);
    RUN_TEST(test_failed_access_counts_only_as_an_error);
    RUN_TEST(test_reset_empties_the_cache_and_the_stats);
    RUN_TEST(test_command_lines_drive_the_simulator);
    RUN_TEST(test_command_lines_that_are_not_commands);
    RUN_TEST(test_page_allocation_is_reported);
    RUN_TEST(test_every_status_has_a_message);
    RUN_TEST(test_bad_configuration_is_rejected);

    return UNITY_END();
}
