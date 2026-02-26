#ifndef API_H
#define API_H

typedef unsigned int uint;

enum LogLevel { LOG_DEBUG = 0, LOG_INFO = 1, LOG_WARN = 2, LOG_ERROR = 3 };

struct Logger;

struct Logger *logger_create(enum LogLevel min_level);
void logger_log(struct Logger *lg, enum LogLevel level, const char *msg);
void logger_destroy(struct Logger *lg);
uint logger_count(const struct Logger *lg);

#endif
