#include "../include/cli.h"
#include "../include/config.h"
#include "../include/log.h"
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

void set_option_defaults(options_t *opts){
    opts->mode = MODE_NONE;
    opts->config_path = DEFAULT_CONFIG_PATH;
    opts->config_path_given = false;
    opts->log_level = LOG_NORMAL;
    opts->log_level_given = false;
    opts->block_size = DEFAULT_BLOCK_SIZE;
    opts->block_size_given = false;
}

/**
 * @brief Parses a positive whole number of bytes.
 *
 * @return int 0, or -1 if the text is not one
 *
 * getopt_long hands back the text of an argument and takes no view on what it
 * should contain, so the checking is still ours. Only positivity is tested here.
 * Whether a size is a power of two is the engine's rule, and sim_init enforces it
 * for the config file and the command line alike rather than each entry point
 * having its own opinion.
 */
static int parse_positive_int(const char *text, int *out){
    errno = 0;
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if(end == text || *end != '\0' || errno == ERANGE || value <= 0 || value > INT_MAX){
        return -1;
    }
    *out = (int)value;
    return 0;
}

/**
 * @brief Rejects a required value that is present in form but empty.
 *
 * @return bool true if the value was empty, and the complaint has been made
 *
 * getopt_long treats --flag= and --flag "" as an argument that was supplied, so
 * it reports no missing value and hands back an empty string. Catching that here
 * keeps an empty path from reaching whoever opens it, where the failure can only
 * be reported with nothing to name.
 */
static bool empty_value(const char *name, const char *value){
    if(value && *value != '\0'){
        return false;
    }
    log_error("Error: %s was given an empty value\n", name);
    return true;
}

//Identifiers for the long-only options, above any char so they cannot collide
//with a short option's letter.
enum {
    OPT_CONFIG = 1000,
    OPT_BLOCK_SIZE
};

static const struct option LONG_OPTIONS[] = {
    {"interactive", no_argument,       NULL, 'i'},
    {"help",        no_argument,       NULL, 'h'},
    {"verbose",     no_argument,       NULL, 'v'},
    {"quiet",       no_argument,       NULL, 'q'},
    {"config",      required_argument, NULL, OPT_CONFIG},
    {"block-size",  required_argument, NULL, OPT_BLOCK_SIZE},
    {NULL,          0,                 NULL, 0}
};

//A leading ':' asks for ':' on a missing argument rather than '?', which is what
//lets a value that was left out be reported differently from an option nobody
//recognises. opterr is cleared so the complaints below are the only ones printed.
#define SHORT_OPTIONS ":ihvq"

int parse_args(int argc, char **argv, options_t *opts){
    //getopt_long keeps its position in a global, so it has to be rewound before
    //each parse. Without this a second call in one process - a test, say - would
    //resume wherever the first one stopped.
    optind = 1;
    opterr = 0;

    int c;
    while((c = getopt_long(argc, argv, SHORT_OPTIONS, LONG_OPTIONS, NULL)) != -1){
        switch(c){
            case 'h':
                //help wins over anything else on the line: someone who asked how
                //to use the program wants an answer, not a run
                opts->mode = MODE_HELP;
                return 0;

            case 'i':
                opts->mode = MODE_INTERACTIVE;
                break;

            case 'v':
                opts->log_level = LOG_VERBOSE;
                opts->log_level_given = true;
                break;

            case 'q':
                opts->log_level = LOG_QUIET;
                opts->log_level_given = true;
                break;

            case OPT_CONFIG:
                if(empty_value("--config", optarg)){
                    return -1;
                }
                opts->config_path = optarg;
                opts->config_path_given = true;
                break;

            case OPT_BLOCK_SIZE:
                if(empty_value("--block-size", optarg)){
                    return -1;
                }
                if(parse_positive_int(optarg, &opts->block_size) != 0){
                    log_error("Error: --block-size needs a positive whole number of "
                              "bytes (got '%s')\n", optarg);
                    return -1;
                }
                opts->block_size_given = true;
                break;

            //no short option here takes a value, so only a long one can be
            //missing its argument, and the argument as written names it
            case ':':
                log_error("Error: %s needs a value\n", argv[optind - 1]);
                return -1;

            //optopt gives the letter for a short option, which names the culprit
            //inside a bundle like -vx instead of blaming the whole bundle. For a
            //long option it holds a val above any character, so fall back to the
            //argument as the user wrote it.
            case '?':
                if(optopt > 0 && optopt <= UCHAR_MAX){
                    log_error("Error: unknown option '-%c'\n", optopt);
                }
                else{
                    log_error("Error: unknown option '%s'\n", argv[optind - 1]);
                }
                return -1;

            default:
                //unreachable: every value the table can return is handled above
                return -1;
        }
    }

    //whatever is left is positional. A trace file will live here; until the trace
    //runner exists, say that plainly rather than calling it an unknown option.
    if(optind < argc){
        log_error("Error: trace files are not supported yet ('%s'). "
                  "Use --interactive for now.\n", argv[optind]);
        return -1;
    }

    return 0;
}

void print_usage(const char *program){
    const char *name = program ? program : "cache_sim";

    printf("Usage: %s --interactive [options]\n", name);
    printf("\nModes\n");
    printf("  -i, --interactive    step through accesses one command at a time,\n");
    printf("                       narrating what the cache does with each one\n");
    printf("\nOptions\n");
    printf("      --block-size N   bytes per block; a power of two (default: %d)\n",
           DEFAULT_BLOCK_SIZE);
    printf("      --config PATH    read settings from PATH (default: %s)\n",
           DEFAULT_CONFIG_PATH);
    printf("  -v, --verbose        narrate the internals as well\n");
    printf("  -q, --quiet          print only what was explicitly asked for\n");
    printf("  -h, --help           show this message\n");
    printf("\nInteractive mode narrates every access by default; -q turns that off.\n");
}
