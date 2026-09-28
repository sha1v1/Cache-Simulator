#include "../include/config.h"
#include <string.h>

void set_config_defaults(config_t *config){
    config->main_memory_size = DEFAULT_MAIN_MEMORY_SIZE;
    config->lines_per_set = DEFAULT_ASSOCIATIVITY;
    config->block_size = DEFAULT_BLOCK_SIZE;
    config->replacement_policy = POLICY_LRU;
    config->seed = DEFAULT_SEED;
    //last, because it is derived from the two fields above
    config_derive_sets(config, DEFAULT_CACHE_SIZE);
}

int config_derive_sets(config_t *config, int cache_size){
    if(config->block_size <= 0 || config->lines_per_set <= 0 || cache_size <= 0){
        return -1;
    }

    //a set bigger than the whole cache cannot fit even once. Tested by division,
    //because block_size * lines_per_set is exactly the product that can overflow
    if(config->lines_per_set > cache_size / config->block_size){
        return -1;
    }

    //bytes one set holds: one block per way. At most cache_size, after the above
    int bytes_per_set = config->block_size * config->lines_per_set;

    //a size that is not a whole number of sets would leave a partial set, which
    //the address decoding cannot describe - there is no fraction of a set index
    if(cache_size % bytes_per_set != 0){
        return -1;
    }

    config->num_sets = cache_size / bytes_per_set;
    return 0;
}

int config_cache_size(const config_t *config){
    return config->num_sets * config->lines_per_set * config->block_size;
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
