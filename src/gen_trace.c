/*
 * Emits memory access traces for cache_sim to run.
 *
 * Deliberately knows nothing about caches. A trace describes what a program
 * touches; how a cache copes with that is the simulator's question, and keeping
 * the two apart is what makes one trace comparable across configurations. If the
 * workload changed with the cache, every row of a sweep would be a different
 * experiment.
 *
 *     ./build/gen_trace matmul-naive 64 | ./build/cache_sim - --size 4096
 */
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//Bytes per access for the array walks, and per matrix element. Four is an int,
//eight a double; both are common and neither is a cache parameter.
#define DEFAULT_WORD    4
#define DEFAULT_ELEMENT 8
#define MAX_ACCESS_SIZE 256

static uint64_t g_highest = 0;   //highest byte any access touched
static uint64_t g_accesses = 0;

//Records an access. The byte count goes out with it: an access straddling a block
//boundary is two cache lookups, and only the simulator knows where the boundaries
//are, so the width has to travel with the address.
static void emit(char op, uint64_t addr, uint64_t bytes){
    uint64_t last = addr + bytes - 1;
    if(last > g_highest){
        g_highest = last;
    }
    g_accesses++;
    printf("%c 0x%" PRIx64 " %" PRIu64 "\n", op, addr, bytes);
}

static void die(const char *fmt, ...){
    va_list args;
    fprintf(stderr, "Error: ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    exit(1);
}

//A positional parameter, or its default when the argument was not given.
static uint64_t param(int argc, char **argv, int index, uint64_t fallback,
                      const char *name){
    if(index >= argc){
        return fallback;
    }
    if(argv[index][0] == '-'){
        die("%s must be a positive whole number", name);
    }
    errno = 0;
    char *end = NULL;
    unsigned long long value = strtoull(argv[index], &end, 0);
    if(end == argv[index] || *end != '\0' || errno == ERANGE || value == 0){
        die("%s must be a positive whole number", name);
    }
    return (uint64_t)value;
}

static void reject_extra_args(int argc, int maximum, const char *workload){
    if(argc > maximum){
        die("%s received too many parameters", workload);
    }
}

static void validate_region(uint64_t bytes, uint64_t word){
    if(word > bytes){
        die("WORD cannot exceed BYTES");
    }
    if(word > MAX_ACCESS_SIZE){
        die("WORD cannot exceed %d bytes", MAX_ACCESS_SIZE);
    }
    if(bytes > (uint64_t)UINT_MAX + 1u){
        die("BYTES exceeds the simulator's address space");
    }
}

static uint64_t checked_product(uint64_t left, uint64_t right, const char *name){
    if(right != 0 && left > UINT64_MAX / right){
        die("%s is too large", name);
    }
    return left * right;
}

/* ---------------------------------------------------------------- array walks */

//One pass over a region, in word-sized reads. Every block is touched once and in
//order, so the misses are compulsory and the hit rate is set by how many words
//share a block: pure spatial locality.
static void sequential(uint64_t base, uint64_t bytes, uint64_t word){
    for(uint64_t offset = 0; offset <= bytes - word; offset += word){
        emit('R', base + offset, word);
    }
}

//The same walk, repeated. If the region fits in the cache the later passes hit
//throughout; if it does not, each pass evicts what the next one wants, and the
//misses that result are capacity misses - they would happen however the cache
//were organised.
static void loop_region(uint64_t base, uint64_t bytes, uint64_t passes,
                        uint64_t word){
    for(uint64_t pass = 0; pass < passes; pass++){
        sequential(base, bytes, word);
    }
}

//Reads spaced by a fixed stride, wrapping at the end of the region. A stride that
//is a multiple of the bytes one set spans lands every access on the same set,
//which produces conflict misses while most of the cache stands empty.
static void strided(uint64_t base, uint64_t bytes, uint64_t stride,
                    uint64_t count, uint64_t word){
    uint64_t offset = 0;
    for(uint64_t i = 0; i < count; i++){
        emit('R', base + offset, word);
        uint64_t next = offset + stride;
        if(next > bytes - word){
            //step forward a word on wrapping, so the walk does not retrace the
            //same handful of addresses forever
            offset = (next % stride) + word;
            if(offset > bytes - word){
                offset = 0;
            }
        }
        else{
            offset = next;
        }
    }
}

//Uniformly random reads: no locality of either kind, so nothing but capacity and
//conflict misses once the footprint exceeds the cache. The baseline every other
//pattern should beat.
static void random_region(uint64_t base, uint64_t bytes, uint64_t count,
                          uint64_t word, uint64_t seed){
    srand((unsigned int)seed);
    uint64_t slots = bytes / word;
    for(uint64_t i = 0; i < count; i++){
        uint64_t slot = (uint64_t)rand() % slots;
        emit('R', base + slot * word, word);
    }
}

/* -------------------------------------------------------------------- matmul */

/*
 * C = A x B, three N x N matrices laid out one after another in row-major order.
 *
 * C[i][j] is read once before the k loop and written once after, which is what a
 * compiler does with an accumulator, so the traffic that remains is A and B. A is
 * walked along a row - consecutive addresses - while B is walked down a column,
 * stepping N elements at a time. That column walk is the whole problem: every
 * access lands in a different block, and by the time the next j revisits those
 * blocks they have been evicted.
 */
static void matmul_naive(uint64_t n, uint64_t element){
    uint64_t row = n * element;
    uint64_t matrix = n * row;
    uint64_t a = 0, b = matrix, c = 2 * matrix;

    for(uint64_t i = 0; i < n; i++){
        for(uint64_t j = 0; j < n; j++){
            emit('R', c + i * row + j * element, element);
            for(uint64_t k = 0; k < n; k++){
                emit('R', a + i * row + k * element, element);
                emit('R', b + k * row + j * element, element);
            }
            emit('W', c + i * row + j * element, element);
        }
    }
}

/*
 * The same arithmetic, reordered so the working set is a tile rather than a whole
 * row and column. Each tile of B is brought in once and reused across every i in
 * the tile, instead of being fetched again for every j. The accesses are the same
 * ones; only their order changes, which is precisely why this is worth measuring -
 * the difference in miss rate is attributable to nothing else.
 */
static void matmul_tiled(uint64_t n, uint64_t tile, uint64_t element){
    uint64_t row = n * element;
    uint64_t matrix = n * row;
    uint64_t a = 0, b = matrix, c = 2 * matrix;

    for(uint64_t ii = 0; ii < n; ii += tile){
        uint64_t i_end = (tile < n - ii) ? ii + tile : n;
        for(uint64_t jj = 0; jj < n; jj += tile){
            uint64_t j_end = (tile < n - jj) ? jj + tile : n;
            for(uint64_t kk = 0; kk < n; kk += tile){
                uint64_t k_end = (tile < n - kk) ? kk + tile : n;
                for(uint64_t i = ii; i < i_end; i++){
                    for(uint64_t j = jj; j < j_end; j++){
                        emit('R', c + i * row + j * element, element);
                        for(uint64_t k = kk; k < k_end; k++){
                            emit('R', a + i * row + k * element, element);
                            emit('R', b + k * row + j * element, element);
                        }
                        emit('W', c + i * row + j * element, element);
                    }
                }
            }
        }
    }
}

/* ---------------------------------------------------------------------- main */

static void usage(const char *program){
    printf("Usage: %s WORKLOAD [parameters...]\n", program);
    printf("\nArray walks\n");
    printf("  sequential BYTES [WORD]            one pass, WORD-sized reads\n");
    printf("  loop BYTES PASSES [WORD]           the same pass, repeated\n");
    printf("  strided BYTES STRIDE [COUNT] [WORD]  reads spaced by STRIDE\n");
    printf("  random BYTES COUNT [SEED] [WORD]   uniformly random reads\n");
    printf("\nMatrix multiply, three N x N matrices\n");
    printf("  matmul-naive N [ELEMENT]           textbook loop order\n");
    printf("  matmul-tiled N TILE [ELEMENT]      the same, blocked by TILE\n");
    printf("\nWORD defaults to %d bytes, ELEMENT to %d.\n",
           DEFAULT_WORD, DEFAULT_ELEMENT);
    printf("The trace goes to standard output, with the parameters as a comment:\n");
    printf("  %s matmul-tiled 64 8 | ./build/cache_sim - --size 4096\n", program);
}

int main(int argc, char **argv){
    if(argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0){
        usage(argv[0]);
        return argc < 2 ? 1 : 0;
    }

    const char *workload = argv[1];
    enum { WORK_SEQUENTIAL, WORK_LOOP, WORK_STRIDED, WORK_RANDOM,
           WORK_MATMUL_NAIVE, WORK_MATMUL_TILED } kind;
    uint64_t first = 0, second = 0, third = 0, fourth = 0;

    //Parse and validate everything before writing the trace header. Invalid input
    //must not leave a plausible-looking partial trace on standard output.
    if(strcmp(workload, "sequential") == 0){
        reject_extra_args(argc, 4, workload);
        first = param(argc, argv, 2, 4096, "BYTES");
        second = param(argc, argv, 3, DEFAULT_WORD, "WORD");
        validate_region(first, second);
        kind = WORK_SEQUENTIAL;
    }
    else if(strcmp(workload, "loop") == 0){
        reject_extra_args(argc, 5, workload);
        first = param(argc, argv, 2, 4096, "BYTES");
        second = param(argc, argv, 3, 4, "PASSES");
        third = param(argc, argv, 4, DEFAULT_WORD, "WORD");
        validate_region(first, third);
        kind = WORK_LOOP;
    }
    else if(strcmp(workload, "strided") == 0){
        reject_extra_args(argc, 6, workload);
        first = param(argc, argv, 2, 65536, "BYTES");
        second = param(argc, argv, 3, 1024, "STRIDE");
        if(second > first){
            die("STRIDE cannot exceed BYTES");
        }
        uint64_t default_count = checked_product(4, first / second, "COUNT");
        third = param(argc, argv, 4, default_count, "COUNT");
        fourth = param(argc, argv, 5, DEFAULT_WORD, "WORD");
        validate_region(first, fourth);
        kind = WORK_STRIDED;
    }
    else if(strcmp(workload, "random") == 0){
        reject_extra_args(argc, 6, workload);
        first = param(argc, argv, 2, 65536, "BYTES");
        second = param(argc, argv, 3, 4096, "COUNT");
        third = param(argc, argv, 4, 1, "SEED");
        fourth = param(argc, argv, 5, DEFAULT_WORD, "WORD");
        validate_region(first, fourth);
        if(third > UINT_MAX){
            die("SEED cannot exceed %u", UINT_MAX);
        }
        kind = WORK_RANDOM;
    }
    else if(strcmp(workload, "matmul-naive") == 0 ||
            strcmp(workload, "matmul-tiled") == 0){
        bool tiled = strcmp(workload, "matmul-tiled") == 0;
        reject_extra_args(argc, tiled ? 5 : 4, workload);
        first = param(argc, argv, 2, 32, "N");
        second = tiled ? param(argc, argv, 3, 8, "TILE") : 0;
        third = param(argc, argv, tiled ? 4 : 3, DEFAULT_ELEMENT, "ELEMENT");
        if(tiled && second > first){
            die("TILE cannot exceed N");
        }
        if(third > MAX_ACCESS_SIZE){
            die("ELEMENT cannot exceed %d bytes", MAX_ACCESS_SIZE);
        }
        uint64_t matrix = checked_product(checked_product(first, first, "matrix"),
                                          third, "matrix");
        uint64_t footprint = checked_product(matrix, 3, "matrix footprint");
        if(footprint > (uint64_t)UINT_MAX + 1u){
            die("matrix footprint exceeds the simulator's address space");
        }
        kind = tiled ? WORK_MATMUL_TILED : WORK_MATMUL_NAIVE;
    }
    else{
        die("unknown workload '%s'", workload);
    }

    //buffered generously: these traces run to millions of lines, and the default
    //buffer would mean a write syscall every few accesses
    static char out[1 << 20];
    setvbuf(stdout, out, _IOFBF, sizeof(out));

    //the header is a comment, which cache_sim skips, so a trace says what it is
    printf("# gen_trace");
    for(int i = 1; i < argc; i++){
        printf(" %s", argv[i]);
    }
    printf("\n");

    switch(kind){
        case WORK_SEQUENTIAL:   sequential(0, first, second); break;
        case WORK_LOOP:         loop_region(0, first, second, third); break;
        case WORK_STRIDED:      strided(0, first, second, third, fourth); break;
        case WORK_RANDOM:       random_region(0, first, second, fourth, third); break;
        case WORK_MATMUL_NAIVE: matmul_naive(first, third); break;
        case WORK_MATMUL_TILED: matmul_tiled(first, second, third); break;
    }

    //the footprint, so the reader knows what --memory-size the trace needs
    printf("# %" PRIu64 " accesses, highest address 0x%" PRIx64
           " (%" PRIu64 " bytes of memory needed)\n",
           g_accesses, g_highest, g_highest + 1);
    return 0;
}
