#include <stdarg.h>
#include <stdio.h>
#include <time.h>

#include "../../include/logger.h"

static FILE* log_file = NULL;
static LogLevel min_log_level = LOG_INFO;

static const char* level_strings[] = {
    "DEBUG", "INFO", "WARN", "ERROR"};

/*
 * Initializes logger
 */
void logger_init(const char* log_file_path, LogLevel min_level)
{
    // open file for writing
    log_file = fopen(log_file_path, "a");
    if (log_file == NULL)
    {
        perror("Could not open log file");
        return;
    }

    // set min_level
    min_log_level = min_level;
}

/*
 * Closes logger
 */
void logger_close()
{
    if (log_file)
    {
        fclose(log_file);
        log_file = NULL;
    }
}

/*
 * Logs message to the log file
 */
void log_message(LogLevel level, const char* function, const char* format, ...)
{
    // skip logging
    if (!log_file || (level < min_log_level))
    {
        return;
    }

    // generate time for log
    time_t curr_time;
    struct tm local_time = {0};
    char time_str[20];

    time(&curr_time);
    localtime_r(&curr_time, &local_time);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &local_time);

    // print log header
    fprintf(log_file, "[%s] [%s] [%s] ",
            time_str, level_strings[level], function);

    // print messages
    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);

    // newline and flush buff
    fprintf(log_file, "\n");
    fflush(log_file);
}
