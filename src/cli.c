#include "../include/cli.h"
#include "../include/config.h"
#include "../include/log.h"
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void set_option_defaults(options_t *opts){
    opts->mode = MODE_NONE;
    opts->log_level = LOG_NORMAL;
    opts->log_level_given = false;
    opts->cache_size = DEFAULT_CACHE_SIZE;
    opts->block_size = DEFAULT_BLOCK_SIZE;
    opts->associativity = DEFAULT_ASSOCIATIVITY;
    opts->memory_size = DEFAULT_MAIN_MEMORY_SIZE;
    opts->policy = POLICY_LRU;
    opts->seed = DEFAULT_SEED;
    opts->trace_path = NULL;
    opts->format = FORMAT_HUMAN;
}

/**
 * @brief Parses a positive whole number.
 *
 * @return int 0, or -1 if the text is not one
 *
 * getopt_long hands back the text of an argument and takes no view on what it
 * should contain, so the checking is still ours. Only positivity is tested here.
 * Whether a size is a power of two, or divides into whole sets, is checked where
 * that rule lives - sim_init and build_config - rather than in every option.
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

//Same, for a seed, where zero is a perfectly good value.
static int parse_seed(const char *text, unsigned int *out){
    errno = 0;
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 10);
    if(end == text || *end != '\0' || errno == ERANGE || value > UINT_MAX){
        return -1;
    }
    *out = (unsigned int)value;
    return 0;
}

/**
 * @brief Rejects a required value that is present in form but empty.
 *
 * @return bool true if the value was empty, and the complaint has been made
 *
 * getopt_long treats --flag= and --flag "" as an argument that was supplied, so it
 * reports no missing value and hands back an empty string.
 */
static bool empty_value(const char *name, const char *value){
    if(value && *value != '\0'){
        return false;
    }
    log_error("Error: %s was given an empty value\n", name);
    return true;
}

//Reads a positive integer option, complaining in its own terms on refusal.
static int read_int_option(const char *name, const char *text, int *out,
                           const char *units){
    if(empty_value(name, text)){
        return -1;
    }
    if(parse_positive_int(text, out) != 0){
        log_error("Error: %s needs a positive whole number of %s (got '%s')\n",
                  name, units, text);
        return -1;
    }
    return 0;
}

//Identifiers for the long-only options, above any char so they cannot collide
//with a short option's letter.
enum {
    OPT_SIZE = 1000,
    OPT_BLOCK_SIZE,
    OPT_ASSOCIATIVITY,
    OPT_MEMORY_SIZE,
    OPT_POLICY,
    OPT_SEED,
    OPT_FORMAT
};

static const struct option LONG_OPTIONS[] = {
    {"interactive",   no_argument,       NULL, 'i'},
    {"help",          no_argument,       NULL, 'h'},
    {"verbose",       no_argument,       NULL, 'v'},
    {"quiet",         no_argument,       NULL, 'q'},
    {"size",          required_argument, NULL, OPT_SIZE},
    {"block-size",    required_argument, NULL, OPT_BLOCK_SIZE},
    {"associativity", required_argument, NULL, OPT_ASSOCIATIVITY},
    {"memory-size",   required_argument, NULL, OPT_MEMORY_SIZE},
    {"policy",        required_argument, NULL, OPT_POLICY},
    {"seed",          required_argument, NULL, OPT_SEED},
    {"format",        required_argument, NULL, OPT_FORMAT},
    {NULL,            0,                 NULL, 0}
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

            case OPT_SIZE:
                if(read_int_option("--size", optarg, &opts->cache_size, "bytes")){
                    return -1;
                }
                break;

            case OPT_BLOCK_SIZE:
                if(read_int_option("--block-size", optarg, &opts->block_size, "bytes")){
                    return -1;
                }
                break;

            case OPT_ASSOCIATIVITY:
                if(read_int_option("--associativity", optarg, &opts->associativity,
                                   "lines per set")){
                    return -1;
                }
                break;

            case OPT_MEMORY_SIZE:
                if(read_int_option("--memory-size", optarg, &opts->memory_size, "bytes")){
                    return -1;
                }
                break;

            case OPT_POLICY:
                if(empty_value("--policy", optarg)){
                    return -1;
                }
                if(parse_policy(optarg, &opts->policy) != 0){
                    log_error("Error: --policy expects LRU or RANDOM (got '%s')\n", optarg);
                    return -1;
                }
                break;

            case OPT_SEED:
                if(empty_value("--seed", optarg)){
                    return -1;
                }
                if(parse_seed(optarg, &opts->seed) != 0){
                    log_error("Error: --seed needs a whole number (got '%s')\n", optarg);
                    return -1;
                }
                break;

            case OPT_FORMAT:
                if(empty_value("--format", optarg)){
                    return -1;
                }
                if(strcasecmp(optarg, "human") == 0){
                    opts->format = FORMAT_HUMAN;
                }
                else if(strcasecmp(optarg, "csv") == 0){
                    opts->format = FORMAT_CSV;
                }
                else{
                    log_error("Error: --format expects human or csv (got '%s')\n", optarg);
                    return -1;
                }
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

    //whatever is left is positional: the trace to run, by the usual convention
    //that a program's primary input needs no flag in front of it
    if(optind < argc){
        if(argc - optind > 1){
            log_error("Error: one trace file at a time (also given '%s')\n",
                      argv[optind + 1]);
            return -1;
        }
        //stepping through a trace at the prompt is a worthwhile thing to build,
        //but it should be built deliberately rather than falling out of which
        //flag happened to be written last
        if(opts->mode == MODE_INTERACTIVE){
            log_error("Error: choose one mode: a trace file, or --interactive\n");
            return -1;
        }
        opts->trace_path = argv[optind];
        opts->mode = MODE_TRACE;
    }

    return 0;
}

int build_config(const options_t *opts, config_t *config){
    set_config_defaults(config);
    config->block_size = opts->block_size;
    config->lines_per_set = opts->associativity;
    config->main_memory_size = opts->memory_size;
    config->replacement_policy = opts->policy;
    config->seed = opts->seed;

    if(config_derive_sets(config, opts->cache_size) != 0){
        //naming the nearest usable sizes saves the arithmetic: a set holds one
        //block per way, so the total has to be a whole number of those
        int bytes_per_set = opts->block_size * opts->associativity;
        int below = (opts->cache_size / bytes_per_set) * bytes_per_set;

        log_error("Error: --size %d is not a whole number of sets. With %d-byte "
                  "blocks and %d ways a set holds %d bytes",
                  opts->cache_size, opts->block_size, opts->associativity,
                  bytes_per_set);
        //below is zero for a size under one set, where there is nothing lower to
        //suggest and offering it twice would read as a mistake
        if(below > 0){
            log_error(", so try %d or %d.\n", below, below + bytes_per_set);
        }
        else{
            log_error("; the smallest usable size is %d.\n", bytes_per_set);
        }
        return -1;
    }
    return 0;
}

void print_usage(const char *program){
    const char *name = program ? program : "cache_sim";

    printf("Usage: %s TRACE [options]\n", name);
    printf("       %s --interactive [options]\n", name);
    printf("\nModes\n");
    printf("  TRACE                   run every access in a trace file, then report\n");
    printf("                          the statistics; use - to read standard input\n");
    printf("  -i, --interactive       step through accesses one command at a time,\n");
    printf("                          narrating what the cache does with each one\n");
    printf("\nCache geometry\n");
    printf("      --size N            total cache size in bytes (default: %d)\n",
           DEFAULT_CACHE_SIZE);
    printf("      --block-size N      bytes per block; a power of two (default: %d)\n",
           DEFAULT_BLOCK_SIZE);
    printf("      --associativity N   lines per set (default: %d)\n",
           DEFAULT_ASSOCIATIVITY);
    printf("      --policy NAME       LRU or RANDOM (default: LRU)\n");
    printf("  The number of sets follows from these: size / (block-size x associativity).\n");
    printf("\nOther options\n");
    printf("      --memory-size N     bytes of main memory (default: %d)\n",
           DEFAULT_MAIN_MEMORY_SIZE);
    printf("      --seed N            seed for RANDOM replacement (default: %d)\n",
           DEFAULT_SEED);
    printf("      --format NAME       human or csv, for a trace run (default: human)\n");
    printf("  -v, --verbose           narrate the internals as well\n");
    printf("  -q, --quiet             print only what was explicitly asked for\n");
    printf("  -h, --help              show this message\n");
    printf("\nInteractive mode narrates every access by default; -q turns that off.\n");
    printf("A trace run never narrates per access, whatever the level.\n");
    printf("\nA trace line is an operation, an address and an optional byte count:\n");
    printf("  R 0x1000        W 0x1004 4        I 0x400abc,8        # comment\n");
}
