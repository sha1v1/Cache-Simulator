#ifndef LOG_H
#define LOG_H

/**
 * Output level for the layers that are allowed to write to a terminal: the
 * presentation layer and the front ends. The simulator engine never calls this -
 * it returns what happened as data - so nothing here can be reached from inside
 * a simulated access.
 *
 * The internals, such as which page was allocated or which line was chosen, are
 * worth seeing when following the mechanism and only noise when using it, so the
 * level is set once and every informational message is filtered through it.
 */
typedef enum {
    LOG_QUIET   = 0,   //results the user explicitly asked for, nothing else
    LOG_NORMAL  = 1,   //one line per access: hit/miss, value, eviction
    LOG_VERBOSE = 2    //internals too: pages allocated, lines chosen and filled
} log_level_t;

void set_log_level(log_level_t level);
log_level_t get_log_level(void);

//Printed at LOG_NORMAL and above, on stdout.
void log_info(const char *fmt, ...);

//Printed at LOG_VERBOSE only, on stdout.
void log_verbose(const char *fmt, ...);

//Always printed, whatever the level, on stderr: a failure must not be silenced, and
//diagnostics stay out of a redirected results stream.
void log_error(const char *fmt, ...);

#endif
