#ifndef CLI_H
#define CLI_H

#include <stdbool.h>
#include "log.h"

/**
 * Command line parsing, kept apart from main() so that what the arguments mean
 * is stated in one place and main() is left deciding what to do about them.
 *
 * Nothing here builds a simulator or reads a config file: parsing answers only
 * "what was asked for", which is what lets a bad argument be rejected before any
 * memory is allocated.
 */

//Which front end the arguments asked for. MODE_TRACE will join these once the
//trace driven runner exists; the mode is already explicit so that adding it
//changes no behaviour a user has come to rely on.
typedef enum {
    MODE_NONE,         //no mode was requested: print usage and stop
    MODE_HELP,         //help was asked for outright, which is not a failure
    MODE_INTERACTIVE   //drive the simulator one typed command at a time
} run_mode_t;

typedef struct {
    run_mode_t mode;
    const char *config_path;   //where to read settings from
    bool config_path_given;    //true if the path came from the command line, in
                               //which case failing to open it is an error rather
                               //than a reason to fall back on the defaults
    log_level_t log_level;
    bool log_level_given;      //true if -v/-q was passed, so a mode's own default
                               //applies only when the user expressed no preference
    int block_size;            //bytes per block, when the command line says
    bool block_size_given;     //true if it did, so the config file still has its
                               //say when it did not
} options_t;

//Fills opts with what no arguments at all would mean.
void set_option_defaults(options_t *opts);

/**
 * @brief Reads argv into opts.
 *
 * @param argc argument count, as handed to main()
 * @param argv argument vector, as handed to main()
 * @param opts the options_t to fill; must already hold the defaults
 * @return int 0 if every argument was understood, -1 if one was not
 *
 * The offending argument is reported here, where its spelling is known, rather
 * than handed back for main() to word.
 */
int parse_args(int argc, char **argv, options_t *opts);

//The argument reference. program is argv[0], or NULL for the plain name.
void print_usage(const char *program);

#endif
