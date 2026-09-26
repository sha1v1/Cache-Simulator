#include "../include/cli.h"
#include "../include/config.h"
#include "../include/log.h"
#include <stdio.h>
#include <string.h>

void setOptionDefaults(Options *opts){
    opts->mode = MODE_NONE;
    opts->config_path = DEFAULT_CONFIG_PATH;
    opts->config_path_given = false;
    opts->log_level = LOG_NORMAL;
    opts->log_level_given = false;
}

/**
 * @brief The value written onto an option, as in "--config=cache.txt".
 *
 * @return const char* the text after the '=', or NULL if this argument is not
 *         that option written in the attached form
 */
static const char *attachedValue(const char *arg, const char *name){
    size_t len = strlen(name);
    if(strncmp(arg, name, len) == 0 && arg[len] == '='){
        return arg + len + 1;
    }
    return NULL;
}

//True if arg is this option, written either on its own or with a value attached.
static bool isOption(const char *arg, const char *name){
    return strcmp(arg, name) == 0 || attachedValue(arg, name) != NULL;
}

/**
 * @brief The value of an option that requires one, from either spelling.
 *
 * @param i index of the option; advanced past the value when it is separate
 * @return const char* the value, or NULL if none was supplied
 *
 * Accepting both "--config x" and "--config=x" costs one branch and spares the
 * user having to remember which form this program wanted.
 */
static const char *optionValue(int argc, char **argv, int *i, const char *name){
    const char *attached = attachedValue(argv[*i], name);
    if(attached){
        if(*attached == '\0'){
            logError("Error: %s needs a value, as in '%s=config.txt'\n", name, name);
            return NULL;
        }
        return attached;
    }
    if(*i + 1 >= argc){
        logError("Error: %s needs a value, as in '%s config.txt'\n", name, name);
        return NULL;
    }
    (*i)++;
    return argv[*i];
}

int parseArgs(int argc, char **argv, Options *opts){
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

        if(isOption(arg, "--config")){
            const char *value = optionValue(argc, argv, &i, "--config");
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
            logError("Error: trace files are not supported yet ('%s'). "
                     "Use --interactive for now.\n", arg);
            return -1;
        }

        logError("Error: unknown option '%s'\n", arg);
        return -1;
    }
    return 0;
}

void printUsage(const char *program){
    const char *name = program ? program : "cache_sim";

    printf("Usage: %s --interactive [options]\n", name);
    printf("\nModes\n");
    printf("  -i, --interactive    step through accesses one command at a time,\n");
    printf("                       narrating what the cache does with each one\n");
    printf("\nOptions\n");
    printf("      --config PATH    read settings from PATH (default: %s)\n",
           DEFAULT_CONFIG_PATH);
    printf("  -v, --verbose        narrate the internals as well\n");
    printf("  -q, --quiet          print only what was explicitly asked for\n");
    printf("  -h, --help           show this message\n");
    printf("\nInteractive mode narrates every access by default; -q turns that off.\n");
}
