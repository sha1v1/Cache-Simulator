#include "../include/classify.h"
#include <stdint.h>
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
    if(classifier->seen_capacity > SIZE_MAX / 2){
        return -1;
    }
    size_t capacity = classifier->seen_capacity * 2;
    if(capacity > SIZE_MAX / sizeof(*classifier->seen)){
        return -1;
    }
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
 * @param known filled with whether the block was already there
 * @return int 0 on success, or -1 if the table needed to grow and could not
 *
 * Growth happens before insertion. If allocation fails, the table remains below
 * its maximum load and the caller can fail the access cleanly; continuing to
 * insert would eventually fill every slot and make seen_slot loop forever.
 */
static int seen_add(classifier_t *classifier, unsigned long block, bool *known){
    //A full table has no EMPTY sentinel for seen_slot to stop at. This should be
    //unreachable because growth happens at 70%, but keeps a damaged or exhausted
    //table from turning one access into an infinite loop.
    if(classifier->seen_count >= classifier->seen_capacity){
        return -1;
    }

    size_t i = seen_slot(classifier->seen, classifier->seen_capacity, block);
    if(classifier->seen[i] == block){
        *known = true;
        return 0;
    }

    //Compute floor(capacity * 7 / 10) without overflowing size_t.
    size_t max_count = (classifier->seen_capacity / SEEN_MAX_LOAD_DENOMINATOR)
                     * SEEN_MAX_LOAD_NUMERATOR
                     + ((classifier->seen_capacity % SEEN_MAX_LOAD_DENOMINATOR)
                        * SEEN_MAX_LOAD_NUMERATOR)
                       / SEEN_MAX_LOAD_DENOMINATOR;
    if(classifier->seen_count + 1 > max_count){
        if(seen_grow(classifier) != 0){
            return -1;
        }
        i = seen_slot(classifier->seen, classifier->seen_capacity, block);
    }

    classifier->seen[i] = block;
    classifier->seen_count++;
    *known = false;
    return 0;
}

int classifier_init(classifier_t *classifier, int total_blocks){
    memset(classifier, 0, sizeof(*classifier));

    if(total_blocks <= 0
       || (size_t)total_blocks > SIZE_MAX / sizeof(*classifier->ways)
       || (size_t)total_blocks > SIZE_MAX / sizeof(*classifier->used)){
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

int classifier_access(classifier_t *classifier, unsigned long block,
                      bool allocate, miss_kind_t *kind){
    if(!classifier || !kind || !classifier->seen || classifier->seen_capacity == 0
       || !classifier->ways || !classifier->used || classifier->way_count <= 0){
        return -1;
    }

    //the order matters: seen_add reports whether this is the first sight of the
    //block, so it has to run before anything else records having seen it
    bool known = true;   //a non-allocating access never brings it in
    if(allocate && seen_add(classifier, block, &known) != 0){
        return -1;
    }

    bool shadow_hit = shadow_access(classifier, block, allocate);

    if(!known){
        *kind = MISS_COMPULSORY;
        return 0;
    }
    //the reference had it, so the room existed and the mapping is what denied it
    *kind = shadow_hit ? MISS_CONFLICT : MISS_CAPACITY;
    return 0;
}
