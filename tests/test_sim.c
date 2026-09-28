#include "../src/unity/unity.h"
#include "../include/commands.h"
#include "../include/log.h"
#include "../include/sim.h"
#include "../include/trace.h"
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
    config_t config = {.num_sets = 4, .main_memory_size = 1024,
                       .lines_per_set = 2, .block_size = DEFAULT_BLOCK_SIZE,
                       .replacement_policy = POLICY_LRU};
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
    uint64_t clock_before_write = sim.cache->clock;
    TEST_ASSERT_EQUAL(SIM_OK, sim_write(&sim, 0x40, 'B', &info));
    TEST_ASSERT_EQUAL(ACCESS_HIT, info.result);
    TEST_ASSERT_EQUAL(1, sim.stats.write_hits);
    TEST_ASSERT_EQUAL_UINT64(clock_before_write + 1, sim.cache->clock);

    uint8_t cached = 0;
    TEST_ASSERT_EQUAL(1, check_cache(sim.cache, 0x40, &cached, NULL));
    TEST_ASSERT_EQUAL('B', cached);
    TEST_ASSERT_EQUAL('B', read_from_memory(&sim.memory, 0x40));
    TEST_ASSERT_EQUAL(2, sim.stats.memory_writes);
    TEST_ASSERT_EQUAL(2, sim.stats.memory_write_bytes);
}

void test_write_back_allocate_defers_memory_until_eviction(void) {
    sim_free(&sim);
    config_t config = {.num_sets = 1, .main_memory_size = 1024,
                       .lines_per_set = 1, .block_size = DEFAULT_BLOCK_SIZE,
                       .replacement_policy = POLICY_LRU,
                       .write_policy = WRITE_BACK, .write_allocate = true};
    TEST_ASSERT_EQUAL(SIM_OK, sim_init(&sim, &config));

    int old_value = read_from_memory(&sim.memory, 0x00);
    access_info_t info;
    TEST_ASSERT_EQUAL(SIM_OK, sim_write(&sim, 0x00, 'Z', &info));
    TEST_ASSERT_EQUAL(ACCESS_MISS, info.result);
    TEST_ASSERT_EQUAL(0, sim.stats.memory_writes);
    TEST_ASSERT_EQUAL(old_value, read_from_memory(&sim.memory, 0x00));

    line_t *line = &sim.cache->cache_sets[0].cache_lines[0];
    TEST_ASSERT_TRUE(line->dirty);
    TEST_ASSERT_EQUAL('Z', line->block[0]);

    //One-way and one-set: this different block must evict the dirty one.
    TEST_ASSERT_EQUAL(SIM_OK, sim_read(&sim, 0x20, &info));
    TEST_ASSERT_TRUE(info.evicted);
    TEST_ASSERT_TRUE(info.wrote_back);
    TEST_ASSERT_EQUAL('Z', read_from_memory(&sim.memory, 0x00));
    TEST_ASSERT_EQUAL(1, sim.stats.writebacks);
    TEST_ASSERT_EQUAL(1, sim.stats.memory_writes);
    TEST_ASSERT_EQUAL(DEFAULT_BLOCK_SIZE, sim.stats.memory_write_bytes);
}

void test_write_back_reset_flushes_dirty_lines(void) {
    sim_free(&sim);
    config_t config = {.num_sets = 1, .main_memory_size = 1024,
                       .lines_per_set = 1, .block_size = DEFAULT_BLOCK_SIZE,
                       .replacement_policy = POLICY_LRU,
                       .write_policy = WRITE_BACK, .write_allocate = true};
    TEST_ASSERT_EQUAL(SIM_OK, sim_init(&sim, &config));

    TEST_ASSERT_EQUAL(SIM_OK, sim_write(&sim, 0x00, 'Q', NULL));
    TEST_ASSERT_EQUAL(SIM_OK, sim_flush(&sim));
    TEST_ASSERT_EQUAL('Q', read_from_memory(&sim.memory, 0x00));
    TEST_ASSERT_EQUAL(1, sim.stats.writebacks);
    TEST_ASSERT_FALSE(sim.cache->cache_sets[0].cache_lines[0].dirty);

    TEST_ASSERT_EQUAL(SIM_OK, sim_write(&sim, 0x00, 'R', NULL));
    TEST_ASSERT_EQUAL(SIM_OK, sim_reset(&sim));
    TEST_ASSERT_EQUAL('R', read_from_memory(&sim.memory, 0x00));
    TEST_ASSERT_EQUAL(0, sim.stats.writebacks); //reset clears the previous run
    TEST_ASSERT_EQUAL(0, check_cache(sim.cache, 0x00, NULL, NULL));
}

void test_write_through_allocate_fills_on_miss(void) {
    sim.config.write_allocate = true;
    access_info_t info;

    TEST_ASSERT_EQUAL(SIM_OK, sim_write(&sim, 0x40, 'A', &info));
    TEST_ASSERT_EQUAL(ACCESS_MISS, info.result);
    TEST_ASSERT_TRUE(info.memory_accessed);
    TEST_ASSERT_TRUE(info.line_index >= 0);
    TEST_ASSERT_EQUAL('A', read_from_memory(&sim.memory, 0x40));

    uint8_t cached = 0;
    int way = -1;
    TEST_ASSERT_EQUAL(1, check_cache(sim.cache, 0x40, &cached, &way));
    TEST_ASSERT_EQUAL('A', cached);
    TEST_ASSERT_FALSE(sim.cache->cache_sets[info.set_index].cache_lines[way].dirty);
}

void test_write_back_no_allocate_bypasses_on_miss(void) {
    sim.config.write_policy = WRITE_BACK;
    access_info_t info;

    TEST_ASSERT_EQUAL(SIM_OK, sim_write(&sim, 0x40, 'N', &info));
    TEST_ASSERT_EQUAL(ACCESS_MISS, info.result);
    TEST_ASSERT_EQUAL(-1, info.line_index);
    TEST_ASSERT_EQUAL(0, check_cache(sim.cache, 0x40, NULL, NULL));
    TEST_ASSERT_EQUAL('N', read_from_memory(&sim.memory, 0x40));
    TEST_ASSERT_EQUAL(1, sim.stats.memory_writes);
    TEST_ASSERT_EQUAL(0, sim.stats.writebacks);
}

void test_write_policy_parser(void) {
    write_policy_t policy;
    TEST_ASSERT_EQUAL(0, parse_write_policy("back", &policy));
    TEST_ASSERT_EQUAL(WRITE_BACK, policy);
    TEST_ASSERT_EQUAL(0, parse_write_policy("write-through", &policy));
    TEST_ASSERT_EQUAL(WRITE_THROUGH, policy);
    TEST_ASSERT_EQUAL(-1, parse_write_policy("sometimes", &policy));
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

void test_FIFO_ignores_hits_when_choosing_a_victim(void) {
    sim_free(&sim);
    config_t config = {.num_sets = 1, .main_memory_size = 1024,
                       .lines_per_set = 2, .block_size = DEFAULT_BLOCK_SIZE,
                       .replacement_policy = POLICY_FIFO};
    TEST_ASSERT_EQUAL(SIM_OK, sim_init(&sim, &config));

    TEST_ASSERT_EQUAL(SIM_OK, sim_read(&sim, 0x00, NULL));
    TEST_ASSERT_EQUAL(SIM_OK, sim_read(&sim, 0x20, NULL));
    TEST_ASSERT_EQUAL(SIM_OK, sim_read(&sim, 0x00, NULL));
    TEST_ASSERT_EQUAL(SIM_OK, sim_read(&sim, 0x00, NULL));
    TEST_ASSERT_EQUAL(SIM_OK, sim_read(&sim, 0x40, NULL));

    TEST_ASSERT_EQUAL(0, check_cache(sim.cache, 0x00, NULL, NULL));
    TEST_ASSERT_EQUAL(1, check_cache(sim.cache, 0x20, NULL, NULL));
    TEST_ASSERT_EQUAL(1, check_cache(sim.cache, 0x40, NULL, NULL));
    TEST_ASSERT_EQUAL(1, sim.stats.evictions);
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
    TEST_ASSERT_EQUAL_UINT64(0, sim.cache->clock);
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

    TEST_ASSERT_EQUAL(CMD_OK, run_line("flush"));

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
                       SIM_ERR_MEMORY_SIZE, SIM_ERR_BLOCK_SIZE, SIM_ERR_CACHE_SIZE,
                       SIM_ERR_WRITE_POLICY,
                       SIM_ERR_OUT_OF_MEMORY,
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
    /* designated throughout, so adding a field to config_t cannot quietly move a
       value into the wrong setting and change what these cases are testing */
    config_t base = {.num_sets = 4, .main_memory_size = 1024, .lines_per_set = 2,
                     .block_size = DEFAULT_BLOCK_SIZE, .replacement_policy = POLICY_LRU};

    config_t not_power_of_two = base;
    not_power_of_two.num_sets = 5;
    TEST_ASSERT_EQUAL(SIM_ERR_NUM_SETS, sim_init(&bad, &not_power_of_two));

    config_t unaligned_memory = base;
    unaligned_memory.main_memory_size = 1000;
    TEST_ASSERT_EQUAL(SIM_ERR_MEMORY_SIZE, sim_init(&bad, &unaligned_memory));

    config_t no_lines = base;
    no_lines.lines_per_set = 0;
    TEST_ASSERT_EQUAL(SIM_ERR_LINES_PER_SET, sim_init(&bad, &no_lines));

    /* the block offset is masked out of an address, so a block size that is not a
       power of two has no mask that isolates it */
    config_t odd_block = base;
    odd_block.block_size = 48;
    TEST_ASSERT_EQUAL(SIM_ERR_BLOCK_SIZE, sim_init(&bad, &odd_block));

    config_t no_block = base;
    no_block.block_size = 0;
    TEST_ASSERT_EQUAL(SIM_ERR_BLOCK_SIZE, sim_init(&bad, &no_block));

    /* each field is valid alone, but the size in bytes, 2^40, is not an int */
    config_t too_large = base;
    too_large.num_sets = 1 << 20;
    too_large.lines_per_set = 1 << 10;
    too_large.block_size = 1 << 10;
    TEST_ASSERT_EQUAL(SIM_ERR_CACHE_SIZE, sim_init(&bad, &too_large));

    /* a memory smaller than a single block leaves the first fetch running off the
       end, and is caught as an unaligned size rather than slipping through */
    config_t memory_below_one_block = base;
    memory_below_one_block.block_size = 64;
    memory_below_one_block.main_memory_size = 32;
    TEST_ASSERT_EQUAL(SIM_ERR_MEMORY_SIZE, sim_init(&bad, &memory_below_one_block));

    config_t bad_write_policy = base;
    bad_write_policy.write_policy = (write_policy_t)99;
    TEST_ASSERT_EQUAL(SIM_ERR_WRITE_POLICY, sim_init(&bad, &bad_write_policy));
}

/* 0x000, 0x080 and 0x100 all map to set 0 in a 4-set cache, which has 2 ways. The
   first three are first sights of their blocks; the fourth is 0x000 coming back
   after being evicted while six of the eight lines stood empty, so the room was
   there and only the mapping denied it. */
void test_breakdown_names_a_conflict_miss(void) {
    TEST_ASSERT_EQUAL(SIM_OK, sim_read(&sim, 0x000, NULL));
    TEST_ASSERT_EQUAL(SIM_OK, sim_read(&sim, 0x080, NULL));
    TEST_ASSERT_EQUAL(SIM_OK, sim_read(&sim, 0x100, NULL));
    TEST_ASSERT_EQUAL(SIM_OK, sim_read(&sim, 0x000, NULL));

    TEST_ASSERT_EQUAL(4, sim.stats.read_misses);
    TEST_ASSERT_EQUAL(3, sim.stats.compulsory_misses);
    TEST_ASSERT_EQUAL(0, sim.stats.capacity_misses);
    TEST_ASSERT_EQUAL(1, sim.stats.conflict_misses);
}

/* The three kinds have to account for exactly the read misses: a miss that went
   uncounted, or counted twice, would make the breakdown quietly wrong while every
   individual number still looked plausible. */
void test_breakdown_accounts_for_every_read_miss(void) {
    for(unsigned int addr = 0; addr < 0x300; addr += 0x20) {
        TEST_ASSERT_EQUAL(SIM_OK, sim_read(&sim, addr, NULL));
    }
    for(unsigned int addr = 0; addr < 0x300; addr += 0x20) {
        TEST_ASSERT_EQUAL(SIM_OK, sim_read(&sim, addr, NULL));
    }

    TEST_ASSERT_EQUAL(sim.stats.read_misses,
                      sim.stats.compulsory_misses
                      + sim.stats.capacity_misses
                      + sim.stats.conflict_misses);
}

/* A fully associative cache cannot have a conflict miss: any block may sit in any
   way, so nothing is ever evicted for being in the wrong place. Whatever is left
   once the first sight of each block is accounted for is capacity. */
void test_fully_associative_cache_has_no_conflict_misses(void) {
    sim_free(&sim);
    config_t config = {.num_sets = 1, .main_memory_size = 1024,
                       .lines_per_set = 8, .block_size = DEFAULT_BLOCK_SIZE,
                       .replacement_policy = POLICY_LRU};
    TEST_ASSERT_EQUAL(SIM_OK, sim_init(&sim, &config));

    /* twelve distinct blocks into a cache that holds eight */
    for(unsigned int block = 0; block < 12; block++) {
        TEST_ASSERT_EQUAL(SIM_OK, sim_read(&sim, block * 0x20, NULL));
    }
    /* the first one has been displaced by now, and no mapping is to blame */
    TEST_ASSERT_EQUAL(SIM_OK, sim_read(&sim, 0x000, NULL));

    TEST_ASSERT_EQUAL(12, sim.stats.compulsory_misses);
    TEST_ASSERT_EQUAL(1, sim.stats.capacity_misses);
    TEST_ASSERT_EQUAL(0, sim.stats.conflict_misses);
}

/* Compulsory misses are a property of the workload, not of the cache: every
   distinct block has to be fetched once whatever the shape around it. Three very
   different geometries of the same capacity must agree on the count. */
void test_compulsory_count_does_not_depend_on_the_geometry(void) {
    const int ways[] = {1, 2, 8};
    unsigned long counts[3];

    for(int i = 0; i < 3; i++) {
        sim_free(&sim);
        config_t config = {.num_sets = 8 / ways[i], .main_memory_size = 1024,
                           .lines_per_set = ways[i], .block_size = DEFAULT_BLOCK_SIZE,
                           .replacement_policy = POLICY_LRU};
        TEST_ASSERT_EQUAL(SIM_OK, sim_init(&sim, &config));

        for(unsigned int pass = 0; pass < 3; pass++) {
            for(unsigned int block = 0; block < 10; block++) {
                TEST_ASSERT_EQUAL(SIM_OK, sim_read(&sim, block * 0x20, NULL));
            }
        }
        counts[i] = sim.stats.compulsory_misses;
    }

    TEST_ASSERT_EQUAL(10, counts[0]);
    TEST_ASSERT_EQUAL(counts[0], counts[1]);
    TEST_ASSERT_EQUAL(counts[0], counts[2]);
}

/* reset forgets the history too: a block seen before the reset must count as a
   first sight after it, or the breakdown would describe two runs at once. */
void test_reset_forgets_the_miss_history(void) {
    TEST_ASSERT_EQUAL(SIM_OK, sim_read(&sim, 0x000, NULL));
    TEST_ASSERT_EQUAL(1, sim.stats.compulsory_misses);

    TEST_ASSERT_EQUAL(SIM_OK, sim_reset(&sim));
    TEST_ASSERT_EQUAL(0, sim.stats.compulsory_misses);

    TEST_ASSERT_EQUAL(SIM_OK, sim_read(&sim, 0x000, NULL));
    TEST_ASSERT_EQUAL(1, sim.stats.compulsory_misses);
    TEST_ASSERT_EQUAL(0, sim.stats.conflict_misses);
}

/* A set of 2^30-byte blocks times 4 ways is 2^32 bytes: the product overflowed,
   wrapped to 0, and the size check then divided by it. It is a set bigger than the
   cache, and must be refused as one. */
void test_set_bigger_than_the_cache_is_refused(void) {
    config_t config = {.lines_per_set = 4, .block_size = 1 << 30};
    TEST_ASSERT_EQUAL(-1, config_derive_sets(&config, 1024));

    /* one byte short of a set is refused too, not only the overflowing case */
    config_t almost = {.lines_per_set = 2, .block_size = 32};
    TEST_ASSERT_EQUAL(-1, config_derive_sets(&almost, 63));
    TEST_ASSERT_EQUAL(0, config_derive_sets(&almost, 64));
    TEST_ASSERT_EQUAL(1, almost.num_sets);
}

/* Runs a trace held in a string, the way the trace front end runs a file. */
static int run_trace_text(const char *text, trace_summary_t *summary){
    FILE *stream = fmemopen((void *)text, strlen(text), "r");
    TEST_ASSERT_NOT_NULL(stream);
    int status = run_trace(&sim, stream, "test", summary);
    fclose(stream);
    return status;
}

/* A record whose last byte is past 0xFFFFFFFF used to wrap to address 0, come out
   as zero accesses, and let the run report success. It names no real access. */
void test_record_past_the_top_address_is_malformed(void) {
    trace_summary_t summary;

    TEST_ASSERT_EQUAL(-1, run_trace_text("R 0xFFFFFFFF 2\n", &summary));
    TEST_ASSERT_EQUAL(1, summary.malformed);
    TEST_ASSERT_EQUAL(0, summary.records);
    TEST_ASSERT_EQUAL(0, summary.accesses);
}

/* With 1-byte blocks the top address is block UINT_MAX, where a block <= last
   loop can never end. The record is legal; it must be exactly its two accesses,
   both refused because they are beyond the simulated memory. */
void test_record_ending_at_the_top_address_terminates(void) {
    sim_free(&sim);
    config_t config = {.num_sets = 4, .main_memory_size = 1024,
                       .lines_per_set = 1, .block_size = 1,
                       .replacement_policy = POLICY_LRU};
    TEST_ASSERT_EQUAL(SIM_OK, sim_init(&sim, &config));

    trace_summary_t summary;
    TEST_ASSERT_EQUAL(-1, run_trace_text("R 0xFFFFFFFE 2\n", &summary));
    TEST_ASSERT_EQUAL(0, summary.malformed);
    TEST_ASSERT_EQUAL(1, summary.records);
    TEST_ASSERT_EQUAL(2, summary.accesses);
    TEST_ASSERT_EQUAL(2, summary.failed);
}

/* The prefix of an over-long line can be a complete, valid record. Running that
   prefix would make a trace the parser did not actually read affect the result. */
void test_overlong_trace_line_is_not_run(void) {
    char text[400];
    const char *prefix = "R 0x0 #";
    size_t prefix_len = strlen(prefix);
    memcpy(text, prefix, prefix_len);
    memset(text + prefix_len, 'x', sizeof(text) - prefix_len - 2);
    text[sizeof(text) - 2] = '\n';
    text[sizeof(text) - 1] = '\0';

    trace_summary_t summary;
    TEST_ASSERT_EQUAL(-1, run_trace_text(text, &summary));
    TEST_ASSERT_EQUAL(1, summary.truncated);
    TEST_ASSERT_EQUAL(0, summary.records);
    TEST_ASSERT_EQUAL(0, summary.accesses);
    TEST_ASSERT_EQUAL(0, sim.stats.reads);
}

/* A final line may fill the buffer exactly without ending in a newline. Peeking
   past it must identify EOF rather than rejecting a record that did fit. */
void test_full_final_trace_line_without_newline_is_run(void) {
    char text[256];
    const char *prefix = "R 0x0 #";
    size_t prefix_len = strlen(prefix);
    memcpy(text, prefix, prefix_len);
    memset(text + prefix_len, 'x', sizeof(text) - prefix_len - 1);
    text[sizeof(text) - 1] = '\0';

    trace_summary_t summary;
    TEST_ASSERT_EQUAL(0, run_trace_text(text, &summary));
    TEST_ASSERT_EQUAL(0, summary.truncated);
    TEST_ASSERT_EQUAL(1, summary.records);
    TEST_ASSERT_EQUAL(1, summary.accesses);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_read_miss_then_hit);
    RUN_TEST(test_write_through_no_write_allocate);
    RUN_TEST(test_write_back_allocate_defers_memory_until_eviction);
    RUN_TEST(test_write_back_reset_flushes_dirty_lines);
    RUN_TEST(test_write_through_allocate_fills_on_miss);
    RUN_TEST(test_write_back_no_allocate_bypasses_on_miss);
    RUN_TEST(test_write_policy_parser);
    RUN_TEST(test_eviction_is_counted_once_the_set_is_full);
    RUN_TEST(test_FIFO_ignores_hits_when_choosing_a_victim);
    RUN_TEST(test_failed_access_counts_only_as_an_error);
    RUN_TEST(test_reset_empties_the_cache_and_the_stats);
    RUN_TEST(test_command_lines_drive_the_simulator);
    RUN_TEST(test_command_lines_that_are_not_commands);
    RUN_TEST(test_page_allocation_is_reported);
    RUN_TEST(test_every_status_has_a_message);
    RUN_TEST(test_bad_configuration_is_rejected);
    RUN_TEST(test_breakdown_names_a_conflict_miss);
    RUN_TEST(test_breakdown_accounts_for_every_read_miss);
    RUN_TEST(test_fully_associative_cache_has_no_conflict_misses);
    RUN_TEST(test_compulsory_count_does_not_depend_on_the_geometry);
    RUN_TEST(test_reset_forgets_the_miss_history);
    RUN_TEST(test_set_bigger_than_the_cache_is_refused);
    RUN_TEST(test_record_past_the_top_address_is_malformed);
    RUN_TEST(test_record_ending_at_the_top_address_terminates);
    RUN_TEST(test_overlong_trace_line_is_not_run);
    RUN_TEST(test_full_final_trace_line_without_newline_is_run);

    return UNITY_END();
}
