#include "../include/cli.h"
#include "../include/config.h"
#include "../include/log.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
 * Only positivity is checked here. Whether the value is a power of two is the
 * engine's rule, and sim_init enforces it for the config file and the command
 * line alike rather than each entry point having its own opinion.
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
 * @brief The value written onto an option, as in "--config=cache.txt".
 *
 * @return const char* the text after the '=', or NULL if this argument is not
 *         that option written in the attached form
 */
static const char *attached_value(const char *arg, const char *name){
    size_t len = strlen(name);
    if(strncmp(arg, name, len) == 0 && arg[len] == '='){
        return arg + len + 1;
    }
    return NULL;
}

//True if arg is this option, written either on its own or with a value attached.
static bool is_option(const char *arg, const char *name){
    return strcmp(arg, name) == 0 || attached_value(arg, name) != NULL;
}

/**
 * @brief The value of an option that requires one, from either spelling.
 *
 * @param i index of the option; advanced past the value when it is separate
 * @param example a sample value, so the complaint shows this option's own shape
 *        rather than one borrowed from whichever option was written first
 * @return const char* the value, or NULL if none was supplied
 *
 * Accepting both "--config x" and "--config=x" costs one branch and spares the
 * user having to remember which form this program wanted.
 */
static const char *option_value(int argc, char **argv, int *i, const char *name,
                                const char *example){
    const char *attached = attached_value(argv[*i], name);
    if(attached){
        if(*attached == '\0'){
            log_error("Error: %s needs a value, as in '%s=%s'\n", name, name, example);
            return NULL;
        }
        return attached;
    }
    if(*i + 1 >= argc){
        log_error("Error: %s needs a value, as in '%s %s'\n", name, name, example);
        return NULL;
    }
    (*i)++;
    return argv[*i];
}

int parse_args(int argc, char **argv, options_t *opts){
    for(int i = 1; i < argc; i++){
        const char *arg = argv[i];

        //help wins over anything else on the line: someone who asked how to use
        //the program wants an answer, not a run
        if(strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0){
            opts->mode = MODE_HELP;
            return 0;
        }

        if(strcmp(arg, "-i") == 0 || strcmp(arg, "--interactive") == 0){
            opts->mode = MODE_INTERACTIVE;
            continue;
        }

        if(strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0){
            opts->log_level = LOG_VERBOSE;
            opts->log_level_given = true;
            continue;
        }

        if(strcmp(arg, "-q") == 0 || strcmp(arg, "--quiet") == 0){
            opts->log_level = LOG_QUIET;
            opts->log_level_given = true;
            continue;
        }

        if(is_option(arg, "--block-size")){
            const char *value = option_value(argc, argv, &i, "--block-size", "64");
            if(!value){
                return -1;
            }
            if(parse_positive_int(value, &opts->block_size) != 0){
                log_error("Error: --block-size needs a positive whole number of "
                          "bytes (got '%s')\n", value);
                return -1;
            }
            opts->block_size_given = true;
            continue;
        }

        if(is_option(arg, "--config")){
            const char *value = option_value(argc, argv, &i, "--config", "config.txt");
            if(!value){
                return -1;
            }
            opts->config_path = value;
            opts->config_path_given = true;
            continue;
        }

        //a bare word is what a trace file will look like, so say that plainly
        //rather than calling it an unknown option
        if(arg[0] != '-'){
            log_error("Error: trace files are not supported yet ('%s'). "
                     "Use --interactive for now.\n", arg);
            return -1;
        }

        log_error("Error: unknown option '%s'\n", arg);
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
