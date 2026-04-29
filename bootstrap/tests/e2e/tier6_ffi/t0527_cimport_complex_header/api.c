#include "api.h"
#include <stdio.h>
#include <stdlib.h>

struct Logger {
    enum LogLevel min_level;
    unsigned int count;
};

struct Logger *logger_create(enum LogLevel min_level) {
    struct Logger *lg = (struct Logger *)malloc(sizeof(struct Logger));
    lg->min_level = min_level;
    lg->count = 0;
    return lg;
}

void logger_log(struct Logger *lg, enum LogLevel level, const char *msg) {
    lg->count++;
    if (level >= lg->min_level) {
        const char *level_str;
        switch (level) {
            case LOG_DEBUG: level_str = "DEBUG"; break;
            case LOG_INFO:  level_str = "INFO";  break;
            case LOG_WARN:  level_str = "WARN";  break;
            case LOG_ERROR: level_str = "ERROR"; break;
            default:        level_str = "???";   break;
        }
        printf("[%s] %s\n", level_str, msg);
    }
}

void logger_destroy(struct Logger *lg) {
    free(lg);
}

uint logger_count(const struct Logger *lg) {
    return lg->count;
}
