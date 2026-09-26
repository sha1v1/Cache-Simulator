#ifndef REPORT_H
#define REPORT_H

#include "sim.h"

/**
 * The presentation layer: turns what the engine returns into text.
 *
 * It sits above the engine and below the front ends. Nothing here decides what
 * to do, and nothing in the engine calls it, so a front end that wants a
 * different wording - or machine-readable rows - can ignore this file entirely
 * and format the same structs itself.
 */

//The active configuration, and the running totals.
void report_config(const simulator_t *sim);
void report_stats(const stats_t *stats);

//Every line of the cache, as a table. Unprintable bytes are shown as '.' the way
//hexdump does, since a block holds arbitrary bytes and has no terminator.
void report_cache(const cache_t *cache);

/**
 * @brief How an address divides up, then what the access did with it.
 *
 * @param sim the simulator, for the geometry the address is split by
 * @param op 'R' or 'W', the operation that was performed
 * @param addr the address accessed
 * @param info what the engine reported about it
 *
 * The tag/set/offset split is shown first because it is the reason for the
 * outcome: the set comes out of the middle bits, so seeing them is what makes
 * two addresses colliding look inevitable rather than arbitrary.
 */
void report_access(const simulator_t *sim, char op, unsigned int addr,
                  const access_info_t *info);

//The internals behind that access: whether memory was consulted, which page,
//and what became of the cache line. Printed only at the verbose narration level.
void report_access_detail(const simulator_t *sim, char op, unsigned int addr,
                        const access_info_t *info);

//Why a simulator could not be built, naming the offending setting's value.
void report_startup_error(sim_status_t status, const config_t *config);

//Why a single access failed.
void report_access_error(sim_status_t status, unsigned int addr);

//A config file that could not be read. detail may be NULL, for a file that could
//not be opened at all.
void report_config_file_error(const char *path, const char *detail);

#endif
