#ifndef SIM_H
#define SIM_H

#include <stdbool.h>
#include <stdint.h>
#include "cache.h"
#include "config.h"
#include "memory.h"

/**
 * The simulator engine: one simulated machine, driven entirely through the
 * functions below.
 *
 * Nothing in this layer writes to stdout or stderr. Every outcome is returned
 * as data - a sim_status_t, an access_info_t, the stats_t struct - so that how a result
 * is worded, and whether it is shown at all, is decided by whoever is driving.
 * That is what lets a menu and a command line share this code without either of
 * them being built into it.
 */

//Why an engine call could not do what was asked. SIM_OK is zero, so a call can
//still be checked with a plain "if (status)" where the reason does not matter.
typedef enum {
    SIM_OK = 0,
    SIM_ERR_NUM_SETS,          //num_sets is not a positive power of two
    SIM_ERR_LINES_PER_SET,     //lines_per_set is not positive
    SIM_ERR_MEMORY_SIZE,       //main_memory_size is not a positive multiple of block_size
    SIM_ERR_BLOCK_SIZE,        //block_size is not a positive power of two
    SIM_ERR_OUT_OF_MEMORY,     //an allocation failed
    SIM_ERR_ADDRESS_RANGE,     //the address lies outside main memory
    SIM_ERR_NOT_INITIALIZED    //the simulator was never successfully built
} sim_status_t;

/**
 * @brief The canonical meaning of a status code, as one short phrase.
 *
 * This is the code's definition rather than a message to show: it carries no
 * layout, no punctuation and none of the offending values, which the caller
 * holds and can word as it likes.
 */
const char *sim_status_message(sim_status_t status);

//Running totals for a session. Kept per-simulator rather than in a global so a
//reset can clear them, and so they describe one machine rather than the process.
typedef struct {
    //Accesses that completed, so hits + misses equals reads + writes. An access
    //that failed is counted in errors alone, which keeps a run whose only
    //access was out of range from reporting a 100% miss rate.
    unsigned long reads;
    unsigned long read_hits;
    unsigned long read_misses;
    unsigned long writes;
    unsigned long write_hits;
    unsigned long write_misses;
    unsigned long evictions;        //misses that had to displace a valid line
    unsigned long pages_allocated;  //memory pages brought into existence by an access
    unsigned long errors;           //accesses that failed, e.g. an out-of-range address
} stats_t;

//Everything one simulated machine owns. Bundling it means the read/write logic
//takes no globals and every front end drives the same object.
typedef struct {
    config_t config;      //a copy: the simulator owns its settings
    memory_t memory;
    cache_t *cache;
    stats_t  stats;
} simulator_t;

typedef enum {
    ACCESS_HIT,
    ACCESS_MISS,
    ACCESS_ERROR
} access_result_t;

//What one access did, for the caller to report however it likes. Keeping the
//formatting out of here is what lets one caller print a sentence and another
//print a machine-readable row from the same access.
typedef struct {
    access_result_t result;
    int  value;           //the byte read or written, or -1 on error
    int  set_index;       //set the address mapped to, -1 on error
    int  line_index;      //line filled or updated, -1 if no line was touched
    bool evicted;         //true if the line used held valid data beforehand
    bool page_allocated;  //true if this access is what brought the page into existence
} access_info_t;

/**
 * @brief Validates a configuration and builds a simulator from it.
 *
 * @param sim the simulator_t to initialize
 * @param config the settings to copy in
 * @return sim_status_t SIM_OK, or which setting was unusable
 */
sim_status_t sim_init(simulator_t *sim, const config_t *config);

//Releases the cache and memory. Safe to call on a simulator sim_init() failed on.
void sim_free(simulator_t *sim);

//Empties the cache and zeroes the statistics, leaving main memory as it is.
//The simulator is left untouched if the cache could not be rebuilt.
sim_status_t sim_reset(simulator_t *sim);

/**
 * @brief Reads one byte through the cache, filling a line on a miss.
 *
 * @param sim the simulator to read through
 * @param addr the address to read
 * @param info where the outcome is recorded; may be NULL
 * @return sim_status_t SIM_OK, or why the access failed
 */
sim_status_t sim_read(simulator_t *sim, unsigned int addr, access_info_t *info);

/**
 * @brief Writes one byte, write-through with no-write-allocate.
 *
 * @param sim the simulator to write through
 * @param addr the address to write
 * @param value the byte to store
 * @param info where the outcome is recorded; may be NULL
 * @return sim_status_t SIM_OK, or why the access failed
 */
sim_status_t sim_write(simulator_t *sim, unsigned int addr, uint8_t value, access_info_t *info);

//Bytes of cache the configuration describes, for a caller reporting the setup.
int sim_cache_size(const simulator_t *sim);

#endif
