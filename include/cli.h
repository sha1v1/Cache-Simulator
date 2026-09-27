#ifndef CLI_H
#define CLI_H

#include <stdbool.h>
#include "config.h"
#include "log.h"

/**
 * Command line parsing, kept apart from main() so that what the arguments mean is
 * stated in one place and main() is left deciding what to do about them.
 *
 * The command line is the only way to configure a run. There is no settings file,
 * so a result depends on the arguments alone and nothing about where the program
 * was started from can change it.
 */

//Which front end the arguments asked for.
typedef enum {
    MODE_NONE,         //no mode was requested: print usage and stop
    MODE_HELP,         //help was asked for outright, which is not a failure
    MODE_INTERACTIVE,  //drive the simulator one typed command at a time
    MODE_TRACE         //run a whole trace file, then report what it did
} run_mode_t;

//How a trace run states its results. The two have opposite aims: one is read by
//a person, the other concatenated by a script into something plottable.
typedef enum {
    FORMAT_HUMAN,
    FORMAT_CSV
} output_format_t;

/**
 * What the arguments asked for.
 *
 * The geometry fields hold the values as the user states them - a total size and
 * an associativity - not the set count the cache is built from. build_config
 * performs that derivation once, after the whole line has been read, so that
 * writing --size before or after --block-size cannot change the answer.
 */
typedef struct {
    run_mode_t mode;

    log_level_t log_level;
    bool log_level_given;      //true if -v/-q was passed, so a mode's own default
                               //applies only when the user expressed no preference

    int cache_size;            //total bytes of cache
    int block_size;            //bytes per block
    int associativity;         //lines per set
    int memory_size;           //bytes of main memory
    replacement_policy_t policy;
    unsigned int seed;         //fixed by default, so runs repeat

    const char *trace_path;    //the trace to run, or "-" for standard input;
                               //NULL unless a positional argument was given
    output_format_t format;
} options_t;

//Fills opts with what no arguments at all would mean.
void set_option_defaults(options_t *opts);

/**
 * @brief Reads argv into opts.
 *
 * @return int 0 if every argument was understood, -1 if one was not
 *
 * The offending argument is reported here, where its spelling is known, rather
 * than handed back for main() to word.
 */
int parse_args(int argc, char **argv, options_t *opts);

/**
 * @brief Turns the options into the configuration a simulator is built from.
 *
 * @return int 0, or -1 if the sizes do not describe a whole cache
 *
 * Separate from parse_args because the derivation needs every value, and the
 * command line can supply them in any order.
 */
int build_config(const options_t *opts, config_t *config);

//The argument reference. program is argv[0], or NULL for the plain name.
void print_usage(const char *program);

#endif
