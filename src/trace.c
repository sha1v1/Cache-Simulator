#include "../include/trace.h"
#include "../include/log.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

//Long enough for any sane trace line, with the overflow handling below for the
//rest so a long line cannot come back as two bogus records.
#define TRACE_LINE_MAX 256

//How many bad lines to name before falling silent. The count is still exact; it
//is only the naming that stops, so a wholly unreadable file cannot bury the
//statistics under a million complaints.
#define COMPLAINTS_MAX 5

//One line of a trace.
typedef struct {
    char op;             //'R', 'W' or 'I'
    unsigned int addr;
    int size;            //bytes touched, at least 1
} trace_record_t;

//Whitespace or a comma: a trace may separate its fields with either.
static bool is_separator(char c){
    return isspace((unsigned char)c) || c == ',';
}

/**
 * @brief Reads one line of a trace.
 *
 * @return int 1 if the line held a record, 0 if it held nothing, -1 if it could
 *         not be read
 */
static int parse_trace_line(const char *line, trace_record_t *out){
    const char *p = line;
    while(*p && isspace((unsigned char)*p)){
        p++;
    }
    //nothing to do, and not an error either
    if(*p == '\0' || *p == '#'){
        return 0;
    }

    char op = (char)toupper((unsigned char)*p);
    if(op != 'R' && op != 'W' && op != 'I'){
        return -1;
    }
    p++;
    //the operation is one letter: "READ" is not this format, and accepting it
    //would mean guessing at what else on the line was meant
    if(*p != '\0' && !is_separator(*p)){
        return -1;
    }
    while(*p && is_separator(*p)){
        p++;
    }
    if(*p == '\0'){
        return -1;   //an operation with no address
    }

    //base 16 accepts the 0x prefix, so both 1000 and 0x1000 mean the same address
    //and neither can quietly be read as decimal
    errno = 0;
    char *end = NULL;
    unsigned long addr = strtoul(p, &end, 16);
    if(end == p || errno == ERANGE || addr > UINT_MAX){
        return -1;
    }
    p = end;

    int size = 1;
    while(*p && is_separator(*p)){
        p++;
    }
    if(*p != '\0' && *p != '#'){
        errno = 0;
        long bytes = strtol(p, &end, 10);
        if(end == p || errno == ERANGE || bytes <= 0 || bytes > TRACE_LINE_MAX){
            return -1;
        }
        p = end;
        while(*p && isspace((unsigned char)*p)){
            p++;
        }
        //anything still here was neither a field nor a comment
        if(*p != '\0' && *p != '#'){
            return -1;
        }
        //the last byte must still be an address: past 0xFFFFFFFF the arithmetic
        //would wrap to 0 and describe an access no program could make
        if((unsigned long)bytes - 1 > UINT_MAX - addr){
            return -1;
        }
        size = (int)bytes;
    }

    out->op = op;
    out->addr = (unsigned int)addr;
    out->size = size;
    return 1;
}

/**
 * @brief Issues the cache accesses one record amounts to.
 *
 * A record that straddles a block boundary touches two blocks and is therefore
 * two lookups, each of which can hit or miss on its own. Treating it as one would
 * undercount the misses of exactly the unaligned accesses a real program makes.
 */
static void run_record(simulator_t *sim, const trace_record_t *rec,
                       const char *name, unsigned long lineno,
                       trace_summary_t *summary, unsigned long *complaints){
    int block_size = sim->cache->layout.block_size;
    unsigned int first_block = rec->addr / (unsigned int)block_size;
    //cannot wrap: the parser refuses a record whose last byte is past UINT_MAX
    unsigned int last_block = (rec->addr + (unsigned int)rec->size - 1)
                              / (unsigned int)block_size;

    //counted rather than block <= last_block, which is always true when
    //last_block is UINT_MAX (1-byte blocks at the top address) and never ends
    unsigned int block_count = last_block - first_block + 1;
    for(unsigned int i = 0; i < block_count; i++){
        unsigned int block = first_block + i;
        //the first byte this record touches inside this block
        unsigned int at = (block == first_block)
                        ? rec->addr
                        : block * (unsigned int)block_size;

        sim_status_t status = (rec->op == 'W')
            ? sim_write(sim, at, TRACE_WRITE_BYTE, NULL)
            : sim_read(sim, at, NULL);

        summary->accesses++;

        //the engine has already counted this in stats.errors; naming the first few
        //is what tells a reader whether the trace and the memory size agree
        if(status != SIM_OK){
            summary->failed++;
            if(*complaints < COMPLAINTS_MAX){
                log_error("%s:%lu: 0x%X: %s\n", name, lineno, at,
                          sim_status_message(status));
                (*complaints)++;
            }
        }
    }
}

int run_trace(simulator_t *sim, FILE *stream, const char *name,
              trace_summary_t *summary){
    memset(summary, 0, sizeof(*summary));

    char line[TRACE_LINE_MAX];
    unsigned long lineno = 0;
    unsigned long complaints = 0;

    while(fgets(line, sizeof(line), stream)){
        lineno++;

        //a line longer than the buffer would otherwise return as two, the second
        //of which is not a record the file contains
        if(!strchr(line, '\n') && !feof(stream)){
            int c;
            while((c = fgetc(stream)) != '\n' && c != EOF){
                //discard the remainder
            }
            summary->truncated++;
        }

        trace_record_t rec;
        int status = parse_trace_line(line, &rec);

        if(status == 0){
            summary->skipped++;
            continue;
        }
        if(status < 0){
            summary->malformed++;
            if(complaints < COMPLAINTS_MAX){
                //trailing newline stripped, so the complaint stays on one line
                size_t len = strcspn(line, "\r\n");
                log_error("%s:%lu: cannot read '%.*s'\n", name, lineno,
                          (int)len, line);
                complaints++;
            }
            continue;
        }

        summary->records++;
        if(rec.op == 'I'){
            summary->instructions++;
        }
        run_record(sim, &rec, name, lineno, summary, &complaints);
    }

    if(complaints >= COMPLAINTS_MAX){
        log_error("%s: further complaints suppressed\n", name);
    }

    //a trace only partly read, or partly refused, has not been fully run, and a
    //statistic from it should not be taken as though it had
    return (summary->malformed > 0 || summary->failed > 0) ? -1 : 0;
}
