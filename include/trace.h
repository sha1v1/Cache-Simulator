#ifndef TRACE_H
#define TRACE_H

#include <stdio.h>
#include "sim.h"

/**
 * The trace driven front end: reads a whole workload and reports what it did,
 * printing nothing per access.
 *
 * A trace line names an operation, an address, and optionally how many bytes it
 * touches:
 *
 *     R 0x1000        read one byte
 *     R 0x1000 4      read four bytes
 *     W 0x1004,8      write eight bytes - comma or space, either will do
 *     I 0x400abc      an instruction fetch
 *     # comment       ignored, as are blank lines
 *
 * Addresses are hex with or without 0x. Instruction fetches go through the same
 * cache as data, since there is only one here, but are counted separately so a
 * trace that distinguishes them is not silently flattened.
 */

//The byte a write stores. A trace records where a program wrote, not what, and
//the value cannot change a hit into a miss, so one constant serves.
#define TRACE_WRITE_BYTE 0xA5

//What running a trace amounted to, beyond the statistics the engine keeps.
typedef struct {
    unsigned long records;       //lines that named an access
    unsigned long skipped;       //blank lines and comments
    unsigned long malformed;     //lines that could not be read
    unsigned long instructions;  //records marked as an instruction fetch
    unsigned long accesses;      //cache accesses issued: a record straddling two
                                 //blocks is two of them
    unsigned long truncated;     //lines too long for the buffer
    unsigned long failed;        //accesses the engine refused, e.g. an address
                                 //beyond the simulated memory
} trace_summary_t;

/**
 * @brief Runs every access in a trace through the simulator.
 *
 * @param sim the simulator to drive
 * @param stream the trace to read; not closed here
 * @param name what to call the stream when complaining about a line
 * @param summary filled in with what the run amounted to
 * @return int 0 if the whole trace ran, -1 if any line was unreadable or any
 *         access was refused
 *
 * Both failures are counted and the first few named, rather than stopping the run
 * or being passed over in silence. Either one means some of the trace did not
 * reach the cache, so the statistics describe less than the file did, and a
 * caller that only checks the exit status must not be told otherwise.
 */
int run_trace(simulator_t *sim, FILE *stream, const char *name,
              trace_summary_t *summary);

#endif
