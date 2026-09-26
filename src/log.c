#include "../include/log.h"
#include <stdarg.h>
#include <stdio.h>

static log_level_t current_level = LOG_NORMAL;

void set_log_level(log_level_t level){
    current_level = level;
}

log_level_t get_log_level(void){
    return current_level;
}

void log_info(const char *fmt, ...){
    if(current_level < LOG_NORMAL){
        return;
    }
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
}

void log_verbose(const char *fmt, ...){
    if(current_level < LOG_VERBOSE){
        return;
    }
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
}

void log_error(const char *fmt, ...){
    //stderr is unbuffered while stdout may not be, so flush first to keep an
    //error from jumping ahead of the output it refers to
    fflush(stdout);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}
