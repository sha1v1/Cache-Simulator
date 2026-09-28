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
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//Bytes per access for the array walks, and per matrix element. Four is an int,
//eight a double; both are common and neither is a cache parameter.
#define DEFAULT_WORD    4
#define DEFAULT_ELEMENT 8

static unsigned long g_highest = 0;   //highest byte any access touched
static unsigned long g_accesses = 0;

//Records an access. The byte count goes out with it: an access straddling a block
//boundary is two cache lookups, and only the simulator knows where the boundaries
//are, so the width has to travel with the address.
static void emit(char op, unsigned long addr, unsigned long bytes){
    unsigned long last = addr + bytes - 1;
    if(last > g_highest){
        g_highest = last;
    }
    g_accesses++;
    printf("%c 0x%lx %lu\n", op, addr, bytes);
}

static void die(const char *fmt, const char *arg){
    fprintf(stderr, "Error: ");
    fprintf(stderr, fmt, arg);
    fprintf(stderr, "\n");
    exit(1);
}

//A positional parameter, or its default when the argument was not given.
static unsigned long param(int argc, char **argv, int index, unsigned long fallback,
                           const char *name){
    if(index >= argc){
        return fallback;
    }
    errno = 0;
    char *end = NULL;
    unsigned long value = strtoul(argv[index], &end, 0);
    if(end == argv[index] || *end != '\0' || errno == ERANGE || value == 0){
        die("%s must be a positive whole number", name);
    }
    (void)name;
    return value;
}

/* ---------------------------------------------------------------- array walks */

//One pass over a region, in word-sized reads. Every block is touched once and in
//order, so the misses are compulsory and the hit rate is set by how many words
//share a block: pure spatial locality.
static void sequential(unsigned long base, unsigned long bytes, unsigned long word){
    for(unsigned long offset = 0; offset + word <= bytes; offset += word){
        emit('R', base + offset, word);
    }
}

//The same walk, repeated. If the region fits in the cache the later passes hit
//throughout; if it does not, each pass evicts what the next one wants, and the
//misses that result are capacity misses - they would happen however the cache
//were organised.
static void loop_region(unsigned long base, unsigned long bytes, unsigned long passes,
                        unsigned long word){
    for(unsigned long pass = 0; pass < passes; pass++){
        sequential(base, bytes, word);
    }
}

//Reads spaced by a fixed stride, wrapping at the end of the region. A stride that
//is a multiple of the bytes one set spans lands every access on the same set,
//which produces conflict misses while most of the cache stands empty.
static void strided(unsigned long base, unsigned long bytes, unsigned long stride,
                    unsigned long count, unsigned long word){
    unsigned long offset = 0;
    for(unsigned long i = 0; i < count; i++){
        emit('R', base + offset, word);
        offset += stride;
        if(offset + word > bytes){
            //step forward a word on wrapping, so the walk does not retrace the
            //same handful of addresses forever
            offset = (offset % stride) + word;
            if(offset + word > bytes){
                offset = 0;
            }
        }
    }
}

//Uniformly random reads: no locality of either kind, so nothing but capacity and
//conflict misses once the footprint exceeds the cache. The baseline every other
//pattern should beat.
static void random_region(unsigned long base, unsigned long bytes, unsigned long count,
                          unsigned long word, unsigned long seed){
    srand((unsigned int)seed);
    unsigned long slots = bytes / word;
    for(unsigned long i = 0; i < count; i++){
        unsigned long slot = (unsigned long)rand() % slots;
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
static void matmul_naive(unsigned long n, unsigned long element){
    unsigned long row = n * element;
    unsigned long matrix = n * row;
    unsigned long a = 0, b = matrix, c = 2 * matrix;

    for(unsigned long i = 0; i < n; i++){
        for(unsigned long j = 0; j < n; j++){
            emit('R', c + i * row + j * element, element);
            for(unsigned long k = 0; k < n; k++){
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
static void matmul_tiled(unsigned long n, unsigned long tile, unsigned long element){
    unsigned long row = n * element;
    unsigned long matrix = n * row;
    unsigned long a = 0, b = matrix, c = 2 * matrix;

    for(unsigned long ii = 0; ii < n; ii += tile){
        unsigned long i_end = (ii + tile < n) ? ii + tile : n;
        for(unsigned long jj = 0; jj < n; jj += tile){
            unsigned long j_end = (jj + tile < n) ? jj + tile : n;
            for(unsigned long kk = 0; kk < n; kk += tile){
                unsigned long k_end = (kk + tile < n) ? kk + tile : n;
                for(unsigned long i = ii; i < i_end; i++){
                    for(unsigned long j = jj; j < j_end; j++){
                        emit('R', c + i * row + j * element, element);
                        for(unsigned long k = kk; k < k_end; k++){
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

    if(strcmp(workload, "sequential") == 0){
        unsigned long bytes = param(argc, argv, 2, 4096, "BYTES");
        unsigned long word = param(argc, argv, 3, DEFAULT_WORD, "WORD");
        sequential(0, bytes, word);
    }
    else if(strcmp(workload, "loop") == 0){
        unsigned long bytes = param(argc, argv, 2, 4096, "BYTES");
        unsigned long passes = param(argc, argv, 3, 4, "PASSES");
        unsigned long word = param(argc, argv, 4, DEFAULT_WORD, "WORD");
        loop_region(0, bytes, passes, word);
    }
    else if(strcmp(workload, "strided") == 0){
        unsigned long bytes = param(argc, argv, 2, 65536, "BYTES");
        unsigned long stride = param(argc, argv, 3, 1024, "STRIDE");
        unsigned long count = param(argc, argv, 4, 4 * (bytes / stride ? bytes / stride : 1),
                                    "COUNT");
        unsigned long word = param(argc, argv, 5, DEFAULT_WORD, "WORD");
        strided(0, bytes, stride, count, word);
    }
    else if(strcmp(workload, "random") == 0){
        unsigned long bytes = param(argc, argv, 2, 65536, "BYTES");
        unsigned long count = param(argc, argv, 3, 4096, "COUNT");
        unsigned long seed = param(argc, argv, 4, 1, "SEED");
        unsigned long word = param(argc, argv, 5, DEFAULT_WORD, "WORD");
        random_region(0, bytes, count, word, seed);
    }
    else if(strcmp(workload, "matmul-naive") == 0){
        unsigned long n = param(argc, argv, 2, 32, "N");
        unsigned long element = param(argc, argv, 3, DEFAULT_ELEMENT, "ELEMENT");
        matmul_naive(n, element);
    }
    else if(strcmp(workload, "matmul-tiled") == 0){
        unsigned long n = param(argc, argv, 2, 32, "N");
        unsigned long tile = param(argc, argv, 3, 8, "TILE");
        unsigned long element = param(argc, argv, 4, DEFAULT_ELEMENT, "ELEMENT");
        if(tile > n){
            die("TILE cannot exceed N%s", "");
        }
        matmul_tiled(n, tile, element);
    }
    else{
        die("unknown workload '%s'", workload);
    }

    //the footprint, so the reader knows what --memory-size the trace needs
    printf("# %lu accesses, highest address 0x%lx (%lu bytes of memory needed)\n",
           g_accesses, g_highest, g_highest + 1);
    return 0;
}
