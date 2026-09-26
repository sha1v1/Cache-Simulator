#include "../include/report.h"
#include "../include/log.h"
#include <limits.h>
#include <stdio.h>

void reportConfig(const Simulator *sim){
    const Config *c = &sim->config;
    printf("\nConfiguration\n");
    printf("  sets              : %d\n", c->num_sets);
    printf("  lines per set     : %d\n", c->lines_per_set);
    printf("  block size        : %d bytes\n", BLOCK_SIZE);
    printf("  cache size        : %d bytes\n", simCacheSize(sim));
    printf("  main memory       : %d bytes\n", c->main_memory_size);
    printf("  replacement policy: %s\n", policyName(c->replacement_policy));
    printf("  write policy      : write-through, no-write-allocate\n");
}

/**
 * @brief Percentage of hits among accesses, or 0 when there were none.
 *
 * Guarding the divide keeps a run that made no accesses from printing a NaN.
 */
static double hitRate(unsigned long hits, unsigned long total){
    return total == 0 ? 0.0 : (100.0 * (double)hits / (double)total);
}

void reportStats(const Stats *s){
    unsigned long accesses = s->reads + s->writes;
    unsigned long hits = s->read_hits + s->write_hits;
    unsigned long misses = s->read_misses + s->write_misses;

    printf("\nStatistics\n");
    printf("  accesses    : %lu (%lu reads, %lu writes)\n", accesses, s->reads, s->writes);
    printf("  hits        : %lu (%.2f%%)\n", hits, hitRate(hits, accesses));
    printf("  misses      : %lu (%.2f%%)\n", misses, hitRate(misses, accesses));
    printf("  read hits   : %lu / %lu (%.2f%%)\n",
           s->read_hits, s->reads, hitRate(s->read_hits, s->reads));
    printf("  write hits  : %lu / %lu (%.2f%%)\n",
           s->write_hits, s->writes, hitRate(s->write_hits, s->writes));
    printf("  evictions   : %lu\n", s->evictions);
    printf("  pages used  : %lu\n", s->pages_allocated);
    printf("  errors      : %lu\n", s->errors);
}

void reportCache(const Cache *cache){
    if(!cache || !cache->cache_sets){
        logError("Error: cache is not initialized\n");
        return;
    }

    printf("\n\n*****CACHE STATE*****\n");
    printf("Set | Way  | Valid | Tag     | Block Data\n");
    printf("-----------------------------------------\n");

    for(int i = 0; i < cache->num_sets; i++){
        const Set *set = &cache->cache_sets[i];
        for(int j = 0; j < cache->lines_per_set; j++){
            const Line *line = &set->cache_lines[j];
            printf("%3d | %4d | %5d | %7u | ", i, j, line->valid_bit, line->tag);

            //the block holds arbitrary bytes and has no terminator, so %s would
            //read past the array. Print printable ASCII as-is and stand in a
            //'.' for the rest, the way hexdump does.
            for(size_t k = 0; k < sizeof(line->block); k++){
                uint8_t byte = line->block[k];
                putchar((byte >= 32 && byte <= 126) ? byte : '.');
            }
            putchar('\n');
        }
    }
    printf("-----------------------------------------\n");
}

/**
 * @brief Renders a byte as a quoted character, or as nothing if unprintable.
 *
 * @param value the byte
 * @param buf at least 5 bytes of scratch
 * @return const char* text to append after the numeric value
 *
 * A control byte printed raw would move the cursor around instead of showing
 * itself, so only the printable range is quoted.
 */
static const char *quotedChar(int value, char *buf){
    if(value >= 32 && value <= 126){
        buf[0] = ' ';
        buf[1] = '\'';
        buf[2] = (char)value;
        buf[3] = '\'';
        buf[4] = '\0';
    }
    else{
        buf[0] = '\0';
    }
    return buf;
}

/**
 * @brief The three fields an address splits into, with the width of each.
 *
 * The widths are derived rather than stated: the offset is fixed by the block
 * size, the set field by how many sets there are, and the tag is whatever is
 * left of the address, so none of the three can drift out of step with the
 * geometry actually in use.
 */
static void reportAddressFields(const Simulator *sim, unsigned int addr){
    int num_sets    = sim->config.num_sets;
    int offset_bits = BLOCK_OFFSET_BITS;
    int set_bits    = setIndexBits(num_sets);
    int tag_bits    = (int)(sizeof(unsigned int) * CHAR_BIT) - set_bits - offset_bits;

    logInfo("  0x%X = tag 0x%X | set %d | offset %d   (%d|%d|%d bits)\n",
            addr,
            (unsigned int)getTagBits(addr, num_sets),
            getSetIndex(addr, num_sets),
            getBlockOffset(addr),
            tag_bits, set_bits, offset_bits);
}

void reportAccess(const Simulator *sim, char op, unsigned int addr,
                  const AccessInfo *info){
    char charbuf[5];
    const char *outcome = (info->result == ACCESS_HIT) ? "HIT " : "MISS";

    reportAddressFields(sim, addr);

    logInfo("%c 0x%04X  %s  set %d  value 0x%02X%s",
            op, addr, outcome, info->set_index, info->value,
            quotedChar(info->value, charbuf));

    if(op == 'R'){
        if(info->result == ACCESS_HIT){
            logInfo("  (served from way %d)", info->line_index);
        }
        else{
            logInfo("  (filled way %d, %s)", info->line_index,
                    info->evicted ? "evicting a valid line" : "which was free");
        }
    }
    else{
        //write-through means memory is updated either way; say so, and say when
        //the cache was deliberately left alone
        if(info->result == ACCESS_HIT){
            logInfo("  -> cache way %d + memory", info->line_index);
        }
        else{
            logInfo("  -> memory only (no-write-allocate)");
        }
    }
    logInfo("\n");
}

void reportAccessDetail(const Simulator *sim, char op, unsigned int addr,
                        const AccessInfo *info){
    if(info->result == ACCESS_ERROR){
        return;
    }

    //reportAccess has already named the set, the way and the eviction, so what
    //is left to add is the memory side of the access: whether main memory was
    //consulted at all, and what it gave up when it was.

    //a read hit is answered by the cache alone, so naming a page there would
    //suggest main memory was consulted when the whole point is that it was not.
    //A write always reaches memory, hit or miss, because writes are write-through.
    bool touched_memory = (op == 'W') || (info->result == ACCESS_MISS);

    if(!touched_memory){
        logVerbose("  main memory not consulted\n");
        return;
    }

    //derived here rather than carried in AccessInfo: the address and the memory
    //geometry are both to hand, so the engine need not report what can be
    //recomputed
    logVerbose("  memory page %d%s\n",
               (int)addr / sim->memory.page_size,
               info->page_allocated ? " (allocated by this access)" : "");

    //the reason the next few nearby addresses will hit: a miss does not fetch the
    //byte that was asked for, it fetches the whole block that byte sits in
    if(op == 'R' && info->result == ACCESS_MISS){
        logVerbose("  fetched all %d bytes of the block, not just the byte asked for\n",
                   BLOCK_SIZE);
    }
}

void reportStartupError(SimStatus status, const Config *config){
    //the engine names the rule, the caller supplies the value that broke it
    switch(status){
        case SIM_ERR_NUM_SETS:
            logError("Error: %s (got %d)\n", simStatusMessage(status), config->num_sets);
            break;
        case SIM_ERR_LINES_PER_SET:
            logError("Error: %s (got %d)\n", simStatusMessage(status), config->lines_per_set);
            break;
        case SIM_ERR_MEMORY_SIZE:
            logError("Error: %s of %d (got %d)\n", simStatusMessage(status),
                     BLOCK_SIZE, config->main_memory_size);
            break;
        default:
            logError("Error: %s\n", simStatusMessage(status));
            break;
    }
}

void reportAccessError(SimStatus status, unsigned int addr){
    logError("Error: 0x%X: %s\n", addr, simStatusMessage(status));
}

void reportConfigFileError(const char *path, const char *detail){
    if(detail){
        logError("Error: %s: %s\n", path, detail);
    }
    else{
        logError("Error: could not read %s\n", path);
    }
}
