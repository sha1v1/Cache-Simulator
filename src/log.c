#include "../include/log.h"
#include <stdarg.h>
#include <stdio.h>

static LogLevel current_level = LOG_NORMAL;

void setLogLevel(LogLevel level){
    current_level = level;
}

LogLevel getLogLevel(void){
    return current_level;
}

void logInfo(const char *fmt, ...){
    if(current_level < LOG_NORMAL){
        return;
    }
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
}

void logVerbose(const char *fmt, ...){
    if(current_level < LOG_VERBOSE){
        return;
    }
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
}

void logError(const char *fmt, ...){
    //stderr is unbuffered while stdout may not be, so flush first to keep an
    //error from jumping ahead of the output it refers to
    fflush(stdout);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}
