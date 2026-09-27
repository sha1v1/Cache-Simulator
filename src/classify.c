#include "../include/classify.h"
#include <stdlib.h>
#include <string.h>

//No block number can be this, since a block number is an address divided by a
//block size and an address is an unsigned int. It marks an unused slot, which
//saves a parallel array of occupancy flags.
#define EMPTY ((unsigned long)-1)

//Where the seen set starts, and how full it is allowed to get. Open addressing
//degrades sharply near capacity, so it grows well before then.
#define SEEN_INITIAL 1024
#define SEEN_MAX_LOAD_NUMERATOR 7
#define SEEN_MAX_LOAD_DENOMINATOR 10

/**
 * @brief Spreads a block number across the table.
 *
 * Block numbers from a strided workload differ only in a few high bits, and taking
 * them modulo the capacity would pile every one of them into the same slot.
 * Multiplying by an odd constant and keeping the high bits mixes those differences
 * down into the bits the mask actually looks at.
 */
static size_t hash_block(unsigned long block, size_t capacity){
    unsigned long mixed = block * 0x9E3779B97F4A7C15UL;
    return (size_t)(mixed >> 29) & (capacity - 1);
}

//Finds the slot a block occupies, or the first free slot where it would go.
static size_t seen_slot(const unsigned long *slots, size_t capacity,
                        unsigned long block){
    size_t i = hash_block(block, capacity);
    while(slots[i] != EMPTY && slots[i] != block){
        i = (i + 1) & (capacity - 1);
    }
    return i;
}

static int seen_grow(classifier_t *classifier){
    size_t capacity = classifier->seen_capacity * 2;
    unsigned long *slots = malloc(capacity * sizeof(*slots));
    if(!slots){
        return -1;
    }
    for(size_t i = 0; i < capacity; i++){
        slots[i] = EMPTY;
    }
    //rehash: a slot's position depends on the capacity, so nothing can be copied
    for(size_t i = 0; i < classifier->seen_capacity; i++){
        if(classifier->seen[i] != EMPTY){
            slots[seen_slot(slots, capacity, classifier->seen[i])] = classifier->seen[i];
        }
    }
    free(classifier->seen);
    classifier->seen = slots;
    classifier->seen_capacity = capacity;
    return 0;
}

/**
 * @brief Adds a block to the seen set.
 *
 * @return bool true if it was already there
 *
 * A failure to grow is reported as "already seen", which understates the
 * compulsory count rather than inventing a miss that did not happen. It cannot be
 * signalled upward without giving every access a failure path for something that
 * only affects a statistic.
 */
static bool seen_add(classifier_t *classifier, unsigned long block){
    size_t i = seen_slot(classifier->seen, classifier->seen_capacity, block);
    if(classifier->seen[i] == block){
        return true;
    }

    classifier->seen[i] = block;
    classifier->seen_count++;

    if(classifier->seen_count * SEEN_MAX_LOAD_DENOMINATOR
       > classifier->seen_capacity * SEEN_MAX_LOAD_NUMERATOR){
        seen_grow(classifier);
    }
    return false;
}

int classifier_init(classifier_t *classifier, int total_blocks){
    memset(classifier, 0, sizeof(*classifier));

    if(total_blocks <= 0){
        return -1;
    }

    classifier->seen = malloc(SEEN_INITIAL * sizeof(*classifier->seen));
    classifier->ways = malloc((size_t)total_blocks * sizeof(*classifier->ways));
    classifier->used = malloc((size_t)total_blocks * sizeof(*classifier->used));
    if(!classifier->seen || !classifier->ways || !classifier->used){
        classifier_free(classifier);
        return -1;
    }

    classifier->seen_capacity = SEEN_INITIAL;
    classifier->way_count = total_blocks;
    classifier_reset(classifier);
    return 0;
}

void classifier_free(classifier_t *classifier){
    free(classifier->seen);
    free(classifier->ways);
    free(classifier->used);
    memset(classifier, 0, sizeof(*classifier));
}

void classifier_reset(classifier_t *classifier){
    for(size_t i = 0; i < classifier->seen_capacity; i++){
        classifier->seen[i] = EMPTY;
    }
    classifier->seen_count = 0;

    for(int i = 0; i < classifier->way_count; i++){
        classifier->ways[i] = EMPTY;
        classifier->used[i] = 0;
    }
    classifier->clock = 0;
}

/**
 * @brief Looks the block up in the shadow cache, inserting it on a miss.
 *
 * @return bool true if it was already resident
 *
 * A linear scan over the ways: the shadow cache is fully associative, so there is
 * no set to narrow the search to. That makes this O(blocks) per access, which is
 * the price of the breakdown and is paid once per access rather than per miss.
 */
static bool shadow_access(classifier_t *classifier, unsigned long block,
                          bool allocate){
    classifier->clock++;

    int free_way = -1;
    int oldest = -1;   //-1 until a way holding something has been seen

    for(int i = 0; i < classifier->way_count; i++){
        if(classifier->ways[i] == block){
            classifier->used[i] = classifier->clock;
            return true;
        }
        if(classifier->ways[i] == EMPTY){
            if(free_way < 0){
                free_way = i;
            }
        }
        else if(oldest < 0 || classifier->used[i] < classifier->used[oldest]){
            oldest = i;
        }
    }

    //not resident. Under no-write-allocate a write stops here: it reaches memory
    //without disturbing the cache, and the reference must not be more generous.
    if(!allocate){
        return false;
    }

    int target = (free_way >= 0) ? free_way : oldest;
    classifier->ways[target] = block;
    classifier->used[target] = classifier->clock;
    return false;
}

miss_kind_t classifier_access(classifier_t *classifier, unsigned long block,
                              bool allocate){
    //the order matters: seen_add reports whether this is the first sight of the
    //block, so it has to run before anything else records having seen it
    bool known = allocate ? seen_add(classifier, block)
                          : true;   //a non-allocating access never brings it in

    bool shadow_hit = shadow_access(classifier, block, allocate);

    if(!known){
        return MISS_COMPULSORY;
    }
    //the reference had it, so the room existed and the mapping is what denied it
    return shadow_hit ? MISS_CONFLICT : MISS_CAPACITY;
}
