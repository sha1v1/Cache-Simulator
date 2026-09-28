#ifndef CLASSIFY_H
#define CLASSIFY_H

#include <stdbool.h>
#include <stddef.h>

/**
 * Why a miss missed - the three C's.
 *
 * A miss rate on its own says nothing about which knob to turn. These say it:
 *
 *   compulsory  the block had never been in the cache at all. Unavoidable; the
 *               count is the trace's footprint, and it is the same for every
 *               configuration at a given block size.
 *   capacity    the block had been in and was evicted, and would have been
 *               evicted even by a cache with total freedom of placement. The
 *               working set is larger than the cache: buy more capacity.
 *   conflict    the block had been in and was evicted while room existed
 *               elsewhere, which the set mapping forbade it from using. Buy more
 *               associativity; a bigger cache at the same associativity may not
 *               help at all.
 */
typedef enum {
    MISS_COMPULSORY,
    MISS_CAPACITY,
    MISS_CONFLICT
} miss_kind_t;

/**
 * What separating the three requires, beyond the cache itself.
 *
 * Telling capacity from conflict needs a reference that cannot have conflict
 * misses: a fully associative cache of the same total size, run alongside the real
 * one. If it would have hit, the space existed and the mapping wasted it; if it
 * would have missed too, the space genuinely was not there.
 *
 * Telling compulsory from the other two needs the set of blocks ever brought in,
 * which grows with the trace's footprint rather than with the cache.
 */
typedef struct {
    unsigned long *seen;      //block numbers ever brought in; open addressed
    size_t seen_capacity;     //a power of two, so the mask below is valid
    size_t seen_count;

    unsigned long *ways;      //the shadow cache: one block number per way
    unsigned long *used;      //its LRU timestamps, one per way
    int way_count;            //as many ways as the real cache has blocks
    unsigned long clock;      //ticks once per shadow access
} classifier_t;

/**
 * @brief Prepares a classifier for a cache holding total_blocks blocks.
 *
 * @return int 0, or -1 if allocation failed
 */
int classifier_init(classifier_t *classifier, int total_blocks);

void classifier_free(classifier_t *classifier);

//Empties both structures, for a reset. Keeps the allocations.
void classifier_reset(classifier_t *classifier);

/**
 * @brief Records an access and says how a miss on it would be classified.
 *
 * @param block the block number accessed, i.e. address / block_size
 * @param allocate whether this access would bring the block in on a miss
 * @param kind filled with the classification that applies if the real cache
 *        missed
 * @return int 0 on success, or -1 if the seen-block set could not grow
 *
 * Must be called for every access that reaches the cache, hit or miss: the shadow
 * cache's contents depend on the whole sequence, so skipping the hits would leave
 * it describing a different workload. The caller uses the answer only when the
 * real cache actually missed.
 *
 * allocate is false for a write under no-write-allocate, where nothing is brought
 * in: such an access can still refresh a block already held, but must not insert
 * one, or the reference would be more generous than the cache it stands in for.
 */
int classifier_access(classifier_t *classifier, unsigned long block,
                      bool allocate, miss_kind_t *kind);

#endif
