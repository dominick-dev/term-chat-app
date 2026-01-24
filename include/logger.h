#ifndef LOGGER_H
#define LOGGER_H

typedef enum
{
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR
} LogLevel;

void logger_init(const char* log_file_path, LogLevel min_level);
void logger_close();
void log_message(LogLevel level, const char* function, const char* format, ...);

#define LOG_DEBUG(func, ...) log_message(LOG_DEBUG, func, __VA_ARGS__)
#define LOG_INFO(func, ...) log_message(LOG_INFO, func, __VA_ARGS__)
#define LOG_WARNING(func, ...) log_message(LOG_WARNING, func, __VA_ARGS__)
#define LOG_ERROR(func, ...) log_message(LOG_ERROR, func, __VA_ARGS__)

#endif
