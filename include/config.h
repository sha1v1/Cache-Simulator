#ifndef CONFIG_H
#define CONFIG_H

//Which line a full set gives up on a miss.
typedef enum {
    POLICY_LRU,      //evict the least recently used line
    POLICY_RANDOM    //evict a line chosen at random
} ReplacementPolicy;

//When a write reaches main memory.
typedef enum {
    WRITE_THROUGH,   //write to cache and memory at the same time
    WRITE_BACK       //write to cache now, to memory on eviction
} WritePolicy;

typedef struct {
    int num_sets;
    int main_memory_size;
    int lines_per_set;
    //stored as enums rather than strings: the value is validated once while
    //parsing instead of on every eviction, and there is no fixed-size buffer
    //for an over-long config value to overflow
    ReplacementPolicy replacement_policy;
    WritePolicy write_policy;
} Config;

void readConfigFile(Config *config);

#endif 