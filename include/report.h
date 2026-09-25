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
void reportConfig(const Simulator *sim);
void reportStats(const Stats *stats);

//Every line of the cache, as a table. Unprintable bytes are shown as '.' the way
//hexdump does, since a block holds arbitrary bytes and has no terminator.
void reportCache(const Cache *cache);

/**
 * @brief The one-line summary of an access.
 *
 * @param op 'R' or 'W', the operation that was performed
 * @param addr the address accessed
 * @param info what the engine reported about it
 */
void reportAccess(char op, unsigned int addr, const AccessInfo *info);

//The internals behind that access: whether memory was consulted, which page,
//and what became of the cache line. Printed only at the verbose narration level.
void reportAccessDetail(const Simulator *sim, char op, unsigned int addr,
                        const AccessInfo *info);

//Why a simulator could not be built, naming the offending setting's value.
void reportStartupError(SimStatus status, const Config *config);

//Why a single access failed.
void reportAccessError(SimStatus status, unsigned int addr);

//A config file that could not be read. detail may be NULL, for a file that could
//not be opened at all.
void reportConfigFileError(const char *path, const char *detail);

#endif
