#ifndef  CACHE_H
#define CACHE_H
#include <stdbool.h>
#include "memory.h"

extern unsigned int global_time;

typedef struct {
    bool valid_bit;
    unsigned int tag;   
    uint8_t block[BLOCK_SIZE];   //raw bytes, not a string - no terminator
    unsigned int last_access_time;
} line_t;

typedef struct {
    line_t *cache_lines; //pointer to array of lines
    int lines_per_set;
} set_t;

typedef struct {
    set_t* cache_sets;
    int num_sets;
    int lines_per_set; //just easy to access lol
} cache_t;


cache_t* initialize_cache(config_t *config);
int initialize_sets(set_t* sets, int num_sets, int lines_per_set);
int get_set_index(unsigned int addr, int num_sets);
int get_block_offset(unsigned int addr);
int get_tag_bits(unsigned int addr, int num_sets);
//out_way receives the way within the set that matched, on a hit; may be NULL.
int check_cache(cache_t *cache, unsigned int addr, uint8_t* out_data, int *out_way);

//Number of address bits needed to index num_sets sets, i.e. an exact log2().
//Public because the presentation layer shows how an address divides up.
int set_index_bits(int num_sets);
line_t *handle_line_replacement(cache_t *cache, unsigned int addr, replacement_policy_t policy);
line_t *random_replacement(set_t *set);
line_t* least_recently_used(set_t *set);
void update_cache(line_t *line, int tag_bits, const uint8_t *block_data);
void free_cache(cache_t *cache);

#endif


