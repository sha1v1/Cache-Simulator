#ifndef  CACHE_H
#define CACHE_H
#include <stdbool.h>
#include "memory.h"

extern unsigned int global_time;

/**
 * How an address divides into the three fields the cache indexes by:
 *
 *   +-------------- tag --------------+--- set ---+-- offset --+
 *
 * Worked out once from the geometry rather than on every access, and derived
 * rather than stated, so that no field can drift out of step with the block size
 * and set count actually in use.
 *
 * Both sizes must be powers of two. The set index and the block offset are masked
 * out of the address rather than divided out of it, and only a power of two has a
 * mask that isolates the right bits: at 48 bytes, 47 is 0b101111 and the masking
 * quietly returns nonsense.
 */
typedef struct {
    int block_size;             //bytes per block
    int num_sets;               //sets in the cache
    int offset_bits;            //log2(block_size)
    int set_bits;               //log2(num_sets)
    int tag_shift;              //offset_bits + set_bits: what the tag sits above
    unsigned int offset_mask;   //keeps just the block offset bits
    unsigned int set_mask;      //keeps just the set index, once shifted down
} address_layout_t;

/**
 * @brief Works a layout out from a block size and a set count.
 *
 * @param layout the layout to fill
 * @param block_size bytes per block; must be a positive power of two
 * @param num_sets sets in the cache; must be a positive power of two
 * @return int 0, or -1 if either size is unusable
 */
int address_layout_init(address_layout_t *layout, int block_size, int num_sets);

typedef struct {
    bool valid_bit;
    unsigned int tag;
    uint8_t *block;    //block_size raw bytes, not a string - no terminator.
                       //Points into the single arena the cache allocates, so no
                       //line owns its own block and none has to be freed.
    unsigned int last_access_time;
} line_t;

typedef struct {
    line_t *cache_lines; //pointer to array of lines
    int lines_per_set;
} set_t;

typedef struct {
    set_t* cache_sets;
    int num_sets;
    int lines_per_set;        //just easy to access lol
    address_layout_t layout;  //how this cache reads an address apart
    uint8_t *block_arena;     //one allocation holding every line's block bytes
} cache_t;


cache_t* initialize_cache(config_t *config);

/**
 * @brief Points every line at its slice of the block arena and empties it.
 *
 * @param block_arena num_sets * lines_per_set * block_size bytes, owned by the
 *        caller for as long as the sets are in use
 */
int initialize_sets(set_t* sets, int num_sets, int lines_per_set,
                    uint8_t *block_arena, int block_size);

int get_set_index(const address_layout_t *layout, unsigned int addr);
int get_block_offset(const address_layout_t *layout, unsigned int addr);
unsigned int get_tag_bits(const address_layout_t *layout, unsigned int addr);

//out_way receives the way within the set that matched, on a hit; may be NULL.
int check_cache(cache_t *cache, unsigned int addr, uint8_t* out_data, int *out_way);

line_t *handle_line_replacement(cache_t *cache, unsigned int addr, replacement_policy_t policy);
line_t *random_replacement(set_t *set);
line_t* least_recently_used(set_t *set);
void update_cache(line_t *line, unsigned int tag_bits, const uint8_t *block_data,
                  int block_size);
void free_cache(cache_t *cache);

#endif
