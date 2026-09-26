#include "../include/config.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//Defaults matched to the config.txt shipped with the project, so a run with no
//config file behaves the same as the documented one.
#define DEFAULT_NUM_SETS         4
#define DEFAULT_MAIN_MEMORY_SIZE 1024
#define DEFAULT_LINES_PER_SET    2
#define DEFAULT_POLICY           POLICY_LRU

void set_config_defaults(config_t *config){
    config->num_sets = DEFAULT_NUM_SETS;
    config->main_memory_size = DEFAULT_MAIN_MEMORY_SIZE;
    config->lines_per_set = DEFAULT_LINES_PER_SET;
    config->replacement_policy = DEFAULT_POLICY;
}

const char *policy_name(replacement_policy_t policy){
    //No default case: -Wswitch then warns here if a policy is added to the enum
    //and this function is not updated.
    switch(policy){
        case POLICY_LRU:    return "LRU";
        case POLICY_RANDOM: return "RANDOM";
    }
    return "UNKNOWN";
}

int parse_policy(const char *name, replacement_policy_t *out){
    if(strcasecmp(name, "LRU") == 0){
        *out = POLICY_LRU;
        return 0;
    }
    if(strcasecmp(name, "RANDOM") == 0){
        *out = POLICY_RANDOM;
        return 0;
    }
    return -1;
}

/**
 * @brief Strips leading and trailing whitespace from a string, in place.
 *
 * @param s the string to trim
 * @return char* the first non-whitespace character
 *
 * The key side of "num_sets = 4" keeps the spaces the %[^=] conversion below
 * cannot exclude, so both halves are trimmed before being compared or parsed.
 */
static char *trim(char *s){
    while(*s && isspace((unsigned char)*s)){
        s++;
    }
    char *end = s + strlen(s);
    while(end > s && isspace((unsigned char)end[-1])){
        end--;
    }
    *end = '\0';
    return s;
}

/**
 * @brief Records why a line was rejected, if the caller asked to be told.
 *
 * Kept to one short phrase with no path or line prefix: the caller knows where
 * the text came from and how it wants to introduce it.
 */
static void set_error(char *error, size_t error_size, const char *fmt, ...){
    if(!error || error_size == 0){
        return;
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(error, error_size, fmt, args);
    va_end(args);
}

int read_config_file(config_t *config, const char *path, char *error, size_t error_size){
    FILE* config_file = fopen(path, "r");
    if(!config_file){
        return -1;
    }

    char buffer[128];
    int status = 0;
    //fgets reads a line and stores in buffer after terminating it with \0
    //i.e. buffer stores a valid string
    while(status == 0 && fgets(buffer, sizeof(buffer), config_file)){

        //blank lines and comments hold no setting, so skip them before trying
        //to parse a pair
        char *line = trim(buffer);
        if(*line == '\0' || *line == '#'){
            continue;
        }

        //to store the key-value pair in the current line
        char raw_key[64], raw_value[64];

        //without this check a failed parse would leave key and value unset,
        //and every branch below would then read uninitialized memory
        if(sscanf(line, "%63[^=]=%63s", raw_key, raw_value) != 2){
            set_error(error, error_size, "malformed line: %s", line);
            status = -2;
            break;
        }

        char *key = trim(raw_key);
        char *value = trim(raw_value);

        if(strcmp(key, "num_sets") == 0){
            config->num_sets = atoi(value);
        }
        else if(strcmp(key, "main_memory_size") == 0){
            config->main_memory_size = atoi(value);
        }
        else if(strcmp(key, "lines_per_set") == 0){
            config->lines_per_set = atoi(value);
        }
        else if(strcmp(key, "replacement_policy") == 0){
            if(parse_policy(value, &config->replacement_policy) != 0){
                set_error(error, error_size,
                         "unknown replacement_policy '%s' (expected LRU or RANDOM)", value);
                status = -2;
                break;
            }
        }
        else{
            set_error(error, error_size, "unknown key '%s'", key);
            status = -2;
            break;
        }
    }

    fclose(config_file);
    return status;
}
