#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

//The config file the simulator reads at startup.
#define DEFAULT_CONFIG_PATH "config.txt"

//Which line a full set gives up on a miss.
typedef enum {
    POLICY_LRU,      //evict the least recently used line
    POLICY_RANDOM    //evict a line chosen at random
} ReplacementPolicy;

typedef struct {
    int num_sets;
    int main_memory_size;
    int lines_per_set;
    //stored as enums rather than strings: the value is validated once while
    //parsing instead of on every eviction, and there is no fixed-size buffer
    //for an over-long config value to overflow
    ReplacementPolicy replacement_policy;
} Config;

//Fills config with the built-in defaults. A config file, and then the command
//line, each override what they mention, so the simulator can run with no
//config file present at all.
void setConfigDefaults(Config *config);

/**
 * @brief Applies the settings in a config file on top of whatever config holds.
 *
 * @param config the Config to update in place
 * @param path the file to read
 * @param error where the offending line is described, when one is rejected;
 *        may be NULL, and is left untouched unless -2 is returned
 * @param error_size the size of that buffer
 * @return int Returns:
 *              - 0 on success
 *              - -1 if the file could not be opened
 *              - -2 if a line was malformed or named an unknown key/value
 *
 * The complaint is handed back rather than printed, because only the caller
 * knows where it should go. Reporting failure rather than exiting likewise lets
 * the caller decide what a missing file means: main() treats it as "the defaults
 * stand" rather than as a reason to refuse to start.
 */
int readConfigFile(Config *config, const char *path, char *error, size_t error_size);

//Name of a policy, for help text and the config summary.
const char *policyName(ReplacementPolicy policy);

//Parses "LRU"/"RANDOM" (case-insensitively) into out. Returns 0, or -1 if the
//name is not one of them.
int parsePolicy(const char *name, ReplacementPolicy *out);

#endif
