#include "../src/unity/unity.h"
#include "../include/commands.h"
#include "../include/log.h"
#include "../include/sim.h"
#include <stdlib.h>
#include <string.h>

simulator_t sim;

/* run_command_line tokenizes its argument in place, so a literal cannot be passed
   to it. Copying into a buffer here keeps every test a one-liner. */
static command_status_t run_line(const char *text){
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%s", text);
    return run_command_line(&sim, buffer, false);
}

void setUp(void) {
    /* LRU rather than RANDOM: eviction has to be predictable for a test to be
       able to assert which line was displaced. */
    config_t config = {4, 1024, 2, POLICY_LRU};
    /* the statistics are what these tests read, so keep the narration out of
       the test output */
    set_log_level(LOG_QUIET);
    TEST_ASSERT_EQUAL(SIM_OK, sim_init(&sim, &config));
}

void tearDown(void) {
    sim_free(&sim);
}

void test_read_miss_then_hit(void) {
    access_info_t info;

    TEST_ASSERT_EQUAL(SIM_OK, sim_read(&sim, 0x100, &info));
    TEST_ASSERT_EQUAL(ACCESS_MISS, info.result);
    TEST_ASSERT_FALSE(info.evicted);

    /* the same block, one byte further along: served from the line just filled */
    TEST_ASSERT_EQUAL(SIM_OK, sim_read(&sim, 0x101, &info));
    TEST_ASSERT_EQUAL(ACCESS_HIT, info.result);

    TEST_ASSERT_EQUAL(2, sim.stats.reads);
    TEST_ASSERT_EQUAL(1, sim.stats.read_hits);
    TEST_ASSERT_EQUAL(1, sim.stats.read_misses);
    TEST_ASSERT_EQUAL(0, sim.stats.evictions);
}

void test_write_through_no_write_allocate(void) {
    access_info_t info;

    /* a write to an absent address must not pull the block into the cache */
    TEST_ASSERT_EQUAL(SIM_OK, sim_write(&sim, 0x40, 'A', &info));
    TEST_ASSERT_EQUAL(ACCESS_MISS, info.result);
    TEST_ASSERT_EQUAL(0, check_cache(sim.cache, 0x40, NULL, NULL));
    TEST_ASSERT_EQUAL(1, sim.stats.write_misses);

    /* but it must have reached memory, so the read that follows sees it */
    TEST_ASSERT_EQUAL('A', read_from_memory(&sim.memory, 0x40));

    /* now resident, so the next write is a hit and updates the line too */
    TEST_ASSERT_EQUAL(SIM_OK, sim_read(&sim, 0x40, &info));
    TEST_ASSERT_EQUAL(SIM_OK, sim_write(&sim, 0x40, 'B', &info));
    TEST_ASSERT_EQUAL(ACCESS_HIT, info.result);
    TEST_ASSERT_EQUAL(1, sim.stats.write_hits);

    uint8_t cached = 0;
    TEST_ASSERT_EQUAL(1, check_cache(sim.cache, 0x40, &cached, NULL));
    TEST_ASSERT_EQUAL('B', cached);
    TEST_ASSERT_EQUAL('B', read_from_memory(&sim.memory, 0x40));
}

void test_eviction_is_counted_once_the_set_is_full(void) {
    access_info_t info;

    /* three blocks, all mapping to set 0, in a 2-way cache: the third displaces
       the least recently used of the first two */
    TEST_ASSERT_EQUAL(SIM_OK, sim_read(&sim, 0x000, &info));
    TEST_ASSERT_FALSE(info.evicted);
    TEST_ASSERT_EQUAL(SIM_OK, sim_read(&sim, 0x080, &info));
    TEST_ASSERT_FALSE(info.evicted);

    TEST_ASSERT_EQUAL(SIM_OK, sim_read(&sim, 0x100, &info));
    TEST_ASSERT_EQUAL(ACCESS_MISS, info.result);
    TEST_ASSERT_TRUE(info.evicted);
    TEST_ASSERT_EQUAL(1, sim.stats.evictions);

    /* LRU evicted the older of the two, so that one is gone and the other stays */
    TEST_ASSERT_EQUAL(0, check_cache(sim.cache, 0x000, NULL, NULL));
    TEST_ASSERT_EQUAL(1, check_cache(sim.cache, 0x080, NULL, NULL));
}

void test_failed_access_counts_only_as_an_error(void) {
    access_info_t info;

    /* out of range: it is not a miss, or hits + misses would no longer account
       for exactly the accesses that completed */
    TEST_ASSERT_EQUAL(SIM_ERR_ADDRESS_RANGE, sim_read(&sim, 0xFFFF, &info));
    TEST_ASSERT_EQUAL(ACCESS_ERROR, info.result);

    TEST_ASSERT_EQUAL(0, sim.stats.reads);
    TEST_ASSERT_EQUAL(0, sim.stats.read_misses);
    TEST_ASSERT_EQUAL(1, sim.stats.errors);
}

void test_reset_empties_the_cache_and_the_stats(void) {
    access_info_t info;
    TEST_ASSERT_EQUAL(SIM_OK, sim_read(&sim, 0x100, &info));

    TEST_ASSERT_EQUAL(SIM_OK, sim_reset(&sim));

    TEST_ASSERT_EQUAL(0, check_cache(sim.cache, 0x100, NULL, NULL));
    TEST_ASSERT_EQUAL(0, sim.stats.reads);
    TEST_ASSERT_EQUAL(0, sim.stats.read_misses);
}

void test_command_lines_drive_the_simulator(void) {
    TEST_ASSERT_EQUAL(CMD_OK, run_line("r 0x100"));
    TEST_ASSERT_EQUAL(1, sim.stats.read_misses);

    /* an address without the 0x prefix is still hex, so this is the same block */
    TEST_ASSERT_EQUAL(CMD_OK, run_line("read 101"));
    TEST_ASSERT_EQUAL(1, sim.stats.read_hits);

    /* a one-character value is that character; 0xNN is a byte */
    TEST_ASSERT_EQUAL(CMD_OK, run_line("w 0x100 7"));
    uint8_t cached = 0;
    TEST_ASSERT_EQUAL(1, check_cache(sim.cache, 0x100, &cached, NULL));
    TEST_ASSERT_EQUAL('7', cached);

    TEST_ASSERT_EQUAL(CMD_OK, run_line("write 0x100 0x41"));
    TEST_ASSERT_EQUAL(1, check_cache(sim.cache, 0x100, &cached, NULL));
    TEST_ASSERT_EQUAL(0x41, cached);

    TEST_ASSERT_EQUAL(CMD_QUIT, run_line("q"));
}

void test_command_lines_that_are_not_commands(void) {
    /* blank lines and comments are skipped rather than reported as errors */
    TEST_ASSERT_EQUAL(CMD_OK, run_line(""));
    TEST_ASSERT_EQUAL(CMD_OK, run_line("   "));
    TEST_ASSERT_EQUAL(CMD_OK, run_line("# a comment"));
    TEST_ASSERT_EQUAL(0, sim.stats.reads);

    /* trailing junk must not be read as a truncated address */
    TEST_ASSERT_EQUAL(CMD_SYNTAX_ERROR, run_line("r 0x1g"));
    TEST_ASSERT_EQUAL(CMD_SYNTAX_ERROR, run_line("r"));
    TEST_ASSERT_EQUAL(CMD_SYNTAX_ERROR, run_line("nonsense"));

    /* an ambiguous multi-character value has to say which byte it means */
    TEST_ASSERT_EQUAL(CMD_SYNTAX_ERROR, run_line("w 0x100 abc"));
    TEST_ASSERT_EQUAL(CMD_SYNTAX_ERROR, run_line("w 0x100 0x100"));

    /* a valid command whose address is out of range is understood but fails */
    TEST_ASSERT_EQUAL(CMD_FAILED, run_line("r 0xFFFF"));
}

void test_page_allocation_is_reported(void) {
    access_info_t info;

    /* the first touch of a page is what creates it; the next touch of the same
       page is not, which is how a caller can narrate the fill without the
       engine printing it */
    TEST_ASSERT_EQUAL(SIM_OK, sim_read(&sim, 0x000, &info));
    TEST_ASSERT_TRUE(info.page_allocated);

    TEST_ASSERT_EQUAL(SIM_OK, sim_read(&sim, 0x0A0, &info));
    TEST_ASSERT_FALSE(info.page_allocated);

    /* page size is 256, so this is a different page */
    TEST_ASSERT_EQUAL(SIM_OK, sim_read(&sim, 0x100, &info));
    TEST_ASSERT_TRUE(info.page_allocated);

    TEST_ASSERT_EQUAL(2, sim.stats.pages_allocated);
}

void test_every_status_has_a_message(void) {
    /* a caller wording an error must never be handed an empty string */
    sim_status_t all[] = {SIM_OK, SIM_ERR_NUM_SETS, SIM_ERR_LINES_PER_SET,
                       SIM_ERR_MEMORY_SIZE, SIM_ERR_OUT_OF_MEMORY,
                       SIM_ERR_ADDRESS_RANGE, SIM_ERR_NOT_INITIALIZED};

    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        const char *message = sim_status_message(all[i]);
        TEST_ASSERT_NOT_NULL(message);
        TEST_ASSERT_TRUE(message[0] != '\0');
    }
}

void test_bad_configuration_is_rejected(void) {
    simulator_t bad;

    /* the status says which setting was wrong, so a caller can name it without
       the engine having to print anything */
    config_t not_power_of_two = {5, 1024, 2, POLICY_LRU};
    TEST_ASSERT_EQUAL(SIM_ERR_NUM_SETS, sim_init(&bad, &not_power_of_two));

    config_t unaligned_memory = {4, 1000, 2, POLICY_LRU};
    TEST_ASSERT_EQUAL(SIM_ERR_MEMORY_SIZE, sim_init(&bad, &unaligned_memory));

    config_t no_lines = {4, 1024, 0, POLICY_LRU};
    TEST_ASSERT_EQUAL(SIM_ERR_LINES_PER_SET, sim_init(&bad, &no_lines));
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
