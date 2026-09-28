#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

//Bytes per block when nothing says otherwise: the unit main memory and the cache
//exchange.
#define DEFAULT_BLOCK_SIZE 32

//The machine a run with no arguments describes. Stated as a total size and an
//associativity, the way the command line takes them, rather than as a set count:
//256 / (32 x 2) is 4 sets, which is what these three amount to.
#define DEFAULT_CACHE_SIZE       256
#define DEFAULT_ASSOCIATIVITY    2
#define DEFAULT_MAIN_MEMORY_SIZE 1024

//Fixed rather than taken from the clock: a measurement tool should repeat, and a
//run that wants variety can ask for it.
#define DEFAULT_SEED 1

//Which line a full set gives up on a miss.
typedef enum {
    POLICY_LRU,      //evict the least recently used line
    POLICY_RANDOM,   //evict a line chosen at random
    POLICY_FIFO      //evict the line that entered the set first
} replacement_policy_t;

//When a cached byte is changed, whether main memory is updated immediately or
//only when the dirty line leaves the cache.
typedef enum {
    WRITE_THROUGH,
    WRITE_BACK
} write_policy_t;

/**
 * The settings one simulated machine is built from.
 *
 * num_sets is held rather than a total size because that is what the address
 * decoding needs, but it is not something the user states: the command line takes
 * a total size and an associativity, and num_sets follows from them and the block
 * size. config_derive_sets is what performs that step, in one place.
 */
typedef struct {
    int num_sets;
    int main_memory_size;
    int lines_per_set;
    //must be a power of two: the block offset is masked out of an address rather
    //than divided out, and only a power of two has a mask
    int block_size;
    //stored as an enum rather than a string: the value is validated once while
    //parsing instead of on every eviction, and there is no fixed-size buffer for
    //an over-long value to overflow
    replacement_policy_t replacement_policy;
    write_policy_t write_policy;
    bool write_allocate;       //bring a block into cache after a write miss
    //recorded here rather than used here: the engine draws from the global rand(),
    //which main() seeds. It travels with the configuration so that a reported run
    //carries the one value needed to reproduce a RANDOM policy exactly.
    unsigned int seed;
} config_t;

//Fills config with the built-in defaults, so every field has a value before the
//command line overrides whatever it mentions.
void set_config_defaults(config_t *config);

/**
 * @brief Sets num_sets from a total cache size, using the block size and
 *        associativity already in config.
 *
 * @param config the config to update; block_size and lines_per_set must be set
 * @param cache_size total bytes of cache
 * @return int 0, or -1 if cache_size is not a whole number of
 *         block_size x lines_per_set groups
 *
 * The three are over-determined - any three of size, block size, associativity
 * and set count fix the fourth - so exactly one of them has to be the derived
 * one. Deriving num_sets is what lets a sweep hold total capacity fixed and vary
 * associativity, which is the comparison worth making.
 */
int config_derive_sets(config_t *config, int cache_size);

//Bytes of cache the configuration describes.
int config_cache_size(const config_t *config);

//Name of a policy, for help text and the run summary.
const char *policy_name(replacement_policy_t policy);

//Parses "LRU"/"RANDOM"/"FIFO" (case-insensitively) into out. Returns 0, or -1
//if the name is not one of them.
int parse_policy(const char *name, replacement_policy_t *out);

const char *write_policy_name(write_policy_t policy);
int parse_write_policy(const char *name, write_policy_t *out);

#endif
