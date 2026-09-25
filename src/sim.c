#include "../include/sim.h"
#include <stdlib.h>
#include <string.h>

const char *simStatusMessage(SimStatus status){
    //No default case: -Wswitch then warns here if a status is added to the enum
    //and this function is not updated.
    switch(status){
        case SIM_OK:                  return "ok";
        case SIM_ERR_NUM_SETS:        return "number of sets must be a positive power of two";
        case SIM_ERR_LINES_PER_SET:   return "lines per set must be positive";
        case SIM_ERR_MEMORY_SIZE:     return "main memory size must be a positive multiple of the block size";
        case SIM_ERR_OUT_OF_MEMORY:   return "out of memory";
        case SIM_ERR_ADDRESS_RANGE:   return "address is outside main memory";
        case SIM_ERR_NOT_INITIALIZED: return "simulator is not initialized";
    }
    return "unknown error";
}

/**
 * @brief Resets an AccessInfo to the "nothing happened" state.
 *
 * Every field is set here so a caller reading info after an early error never
 * sees a stale set or line index left over from the previous access.
 */
static void clearInfo(AccessInfo *info){
    if(!info){
        return;
    }
    info->result = ACCESS_ERROR;
    info->value = -1;
    info->set_index = -1;
    info->line_index = -1;
    info->evicted = false;
    info->page_allocated = false;
}

/**
 * @brief Checks a configuration before anything is allocated from it.
 *
 * Validating here rather than inside the cache and memory modules is what lets
 * the engine say which setting was wrong: those modules can only refuse, while
 * this one holds the whole configuration and can name the part at fault.
 */
static SimStatus validateConfig(const Config *config){
    //getSetIndex masks address bits, which only computes blockNumber % num_sets
    //when num_sets is a power of two. n & (n-1) clears the lowest set bit, so
    //zero means only one bit was set.
    if(config->num_sets <= 0 || (config->num_sets & (config->num_sets - 1)) != 0){
        return SIM_ERR_NUM_SETS;
    }
    if(config->lines_per_set <= 0){
        return SIM_ERR_LINES_PER_SET;
    }
    //fetchBlockFromMemory aligns down to a block boundary and always reads
    //BLOCK_SIZE bytes, so a memory that doesn't end on one would have its last
    //block run past the end.
    if(config->main_memory_size <= 0 || config->main_memory_size % BLOCK_SIZE != 0){
        return SIM_ERR_MEMORY_SIZE;
    }
    return SIM_OK;
}

SimStatus simInit(Simulator *sim, const Config *config){
    memset(sim, 0, sizeof(*sim));

    SimStatus status = validateConfig(config);
    if(status != SIM_OK){
        return status;
    }
    sim->config = *config;

    if(initializeMemory(&sim->memory, &sim->config) != 0){
        return SIM_ERR_OUT_OF_MEMORY;   //the sizes are already known to be sound
    }

    sim->cache = initalizeCache(&sim->config);
    if(!sim->cache){
        //memory is already up, so tear it down rather than leaking it on the
        //way out of a failed startup
        freeMemory(&sim->memory);
        return SIM_ERR_OUT_OF_MEMORY;
    }

    return SIM_OK;
}

void simFree(Simulator *sim){
    if(!sim){
        return;
    }
    if(sim->cache){
        freeCache(sim->cache);   //frees the lines, the sets array, and the Cache struct
        sim->cache = NULL;
    }
    //frees the pages and the page table. The Memory struct itself lives inside
    //the Simulator, so there is nothing further to release.
    freeMemory(&sim->memory);
}

SimStatus simReset(Simulator *sim){
    Cache *fresh = initalizeCache(&sim->config);
    if(!fresh){
        //the old cache is still intact, so the simulator stays usable
        return SIM_ERR_OUT_OF_MEMORY;
    }
    freeCache(sim->cache);
    sim->cache = fresh;
    memset(&sim->stats, 0, sizeof(sim->stats));
    return SIM_OK;
}

/**
 * @brief Whether an address can be accessed at all, and the page it falls in.
 *
 * @param page_index set to the page the address belongs to, when in range
 *
 * Checked here rather than left to memory.c so the engine can distinguish a bad
 * address from a failed allocation: the memory module reports both the same way.
 */
static SimStatus checkAddress(const Simulator *sim, unsigned int addr, int *page_index){
    if(!sim->cache || !sim->memory.page_table){
        return SIM_ERR_NOT_INITIALIZED;
    }
    //total_size is validated positive at init, so the cast is safe and keeps
    //this from being a signed/unsigned comparison
    if(addr >= (unsigned int)sim->memory.total_size){
        return SIM_ERR_ADDRESS_RANGE;
    }
    *page_index = (int)addr / sim->memory.page_size;
    return SIM_OK;
}

SimStatus simRead(Simulator *sim, unsigned int addr, AccessInfo *info){
    clearInfo(info);

    int page_index = 0;
    SimStatus status = checkAddress(sim, addr, &page_index);
    if(status != SIM_OK){
        sim->stats.errors++;
        return status;
    }

    //look in cache. The byte comes back through a uint8_t out-parameter and is
    //reported as an int, so a legitimate 0xFF can never look like an error code.
    uint8_t cached_byte = 0;
    int hit = checkCache(sim->cache, addr, &cached_byte);

    int set_index = getSetIndex(addr, sim->cache->num_sets);

    if(hit == 1){
        //counted here rather than on entry: an access that fails is an error and
        //nothing else, so hits + misses always accounts for exactly the accesses
        //that completed
        sim->stats.reads++;
        sim->stats.read_hits++;
        if(info){
            info->result = ACCESS_HIT;
            info->value = cached_byte;
            info->set_index = set_index;
        }
        return SIM_OK;   //successful cache access, didn't have to look into memory
    }

    //noted before the fetch, which is what would create the page: a caller
    //following the mechanism cannot see this happen from the outside
    bool new_page = (sim->memory.page_table[page_index] == NULL);

    //Miss: pull the whole block in from memory, choose a line for it, and fill it.
    uint8_t *block_data = NULL;
    if(fetchBlockFromMemory(&sim->memory, addr, &block_data) != 0 || !block_data){
        sim->stats.errors++;
        return SIM_ERR_OUT_OF_MEMORY;   //the address is already known to be in range
    }

    Line *line = handleLineReplacement(sim->cache, addr, sim->config.replacement_policy);
    if(!line){
        free(block_data);
        sim->stats.errors++;
        return SIM_ERR_NOT_INITIALIZED;
    }

    //read before updateCache overwrites it: a line that already held valid data
    //is being displaced, which is the eviction worth counting. A fill into an
    //empty line is a cold miss and costs nobody their data.
    bool evicted = line->valid_bit;

    Set *set = &sim->cache->cache_sets[set_index];
    int line_index = (int)(line - set->cache_lines);

    updateCache(line, getTagBits(addr, sim->cache->num_sets), block_data);

    int fetched_byte = block_data[getBlockOffset(addr)];
    free(block_data);

    sim->stats.reads++;
    sim->stats.read_misses++;
    if(evicted){
        sim->stats.evictions++;
    }
    if(new_page){
        sim->stats.pages_allocated++;
    }

    if(info){
        info->result = ACCESS_MISS;
        info->value = fetched_byte;
        info->set_index = set_index;
        info->line_index = line_index;
        info->evicted = evicted;
        info->page_allocated = new_page;
    }
    return SIM_OK;
}

SimStatus simWrite(Simulator *sim, unsigned int addr, uint8_t value, AccessInfo *info){
    clearInfo(info);

    int page_index = 0;
    SimStatus status = checkAddress(sim, addr, &page_index);
    if(status != SIM_OK){
        sim->stats.errors++;
        return status;
    }

    int hit = checkCache(sim->cache, addr, NULL);   //is the address already resident?
    bool new_page = (sim->memory.page_table[page_index] == NULL);

    //write-through: every write reaches main memory. Memory goes first so a
    //rejected address cannot leave a cache line holding a byte that main memory
    //never accepted.
    if(writeToMemory(&sim->memory, addr, value) != 1){
        sim->stats.errors++;
        return SIM_ERR_OUT_OF_MEMORY;   //the address is already known to be in range
    }

    int set_index = getSetIndex(addr, sim->cache->num_sets);
    int line_index = -1;

    //no-write-allocate: the cache is touched only when the address is already
    //resident. A miss does not pull the block in.
    if(hit == 1){
        int tag_bits = getTagBits(addr, sim->cache->num_sets);
        int block_offset = getBlockOffset(addr);
        Set *cur_set = &sim->cache->cache_sets[set_index];

        for(int i = 0; i < cur_set->lines_per_set; i++){
            Line *line = &cur_set->cache_lines[i];
            if(line->valid_bit && line->tag == (unsigned int)tag_bits){
                line->block[block_offset] = value;
                line->last_access_time = global_time++;
                line_index = i;
                break;
            }
        }
    }

    sim->stats.writes++;
    if(hit == 1){
        sim->stats.write_hits++;
    }
    else{
        sim->stats.write_misses++;
    }
    if(new_page){
        sim->stats.pages_allocated++;
    }

    if(info){
        info->result = (hit == 1) ? ACCESS_HIT : ACCESS_MISS;
        info->value = value;
        info->set_index = set_index;
        info->line_index = line_index;
        info->page_allocated = new_page;
    }
    return SIM_OK;
}

int simCacheSize(const Simulator *sim){
    return sim->config.num_sets * sim->config.lines_per_set * BLOCK_SIZE;
}
