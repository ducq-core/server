#ifndef _SQLITE_SRV_LOGGER_HEADER__
#define _SQLITE_SRV_LOGGER_HEADER__

#include <ducq_reactor.h>


typedef struct sql_logger_t sql_logger_t;

int sqlite_srv_logger(void *ctx, enum ducq_log_level level, const char *function_name, const char *sender_id, const char *fmt, va_list args);

int create_sql_logger(sql_logger_t **logger, const char *filename, char **errBuffer);
void sql_logger_set_console_log(sql_logger_t *logger, bool to_console);
int sqlite_srv_logger(void *ctx, enum ducq_log_level level, const char *function_name, const char *sender_id, const char *fmt, va_list args);
int free_sql_logger(sql_logger_t *logger);

#endif // _SQLITE_SRV_LOGGER_HEADER__
