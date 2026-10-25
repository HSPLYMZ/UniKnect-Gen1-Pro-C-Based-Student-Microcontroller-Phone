/* Log macros for the vendored at_client.
 *
 * Same names and same LogLevel enum as Quectel's qosa_log.h (at_utils.c has an
 * `extern LogLevel g_debug_level;`), but the sink is our own board console:
 * no ANSI colours, no SD-card log file, level filtered at runtime.
 */
#ifndef QOSA_LOG_H
#define QOSA_LOG_H

typedef enum {
    LOG_VERBOSE,
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
} LogLevel;

/* Minimum emitted level; LOG_VERBOSE=0 emits all, LOG_INFO is the default. */
extern LogLevel g_debug_level;

void qosa_log_write(int level, const char *tag, const char *func,
                    const char *fmt, ...) __attribute__((format(printf, 4, 5)));

#define LOG_V(...) qosa_log_write(LOG_VERBOSE, "VER",   __func__, __VA_ARGS__)
#define LOG_D(...) qosa_log_write(LOG_DEBUG,   "DBG",   __func__, __VA_ARGS__)
#define LOG_I(...) qosa_log_write(LOG_INFO,    "INFO",  __func__, __VA_ARGS__)
#define LOG_W(...) qosa_log_write(LOG_WARN,    "WARN",  __func__, __VA_ARGS__)
#define LOG_E(...) qosa_log_write(LOG_ERROR,   "ERR",   __func__, __VA_ARGS__)

#endif /* QOSA_LOG_H */
