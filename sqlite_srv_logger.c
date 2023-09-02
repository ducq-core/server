#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stdarg.h>
#include <time.h>

#include <syslog.h>
#include <sqlite3.h>


#include "sqlite_srv_logger.h"

typedef struct sql_logger_t {
	sqlite3 *db;
	bool print_to_console;
} sql_logger_t;

#define ERR_MSG_LEN 256

int create_sql_logger(sql_logger_t **logger, const char *filename, char **errBuffer) {
	int rc = 0;
	sqlite3 *db;
	*errBuffer = malloc(ERR_MSG_LEN);
	if(!*errBuffer) return -1;

  rc = sqlite3_open(filename, &db);
	if( rc ) {
    snprintf(*errBuffer, ERR_MSG_LEN, "Can't open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return -1;
  }


	const char create_stmt[] =
		"CREATE TABLE IF NOT EXISTS monitor ("
			"time    TEXT,"
			"level   TEXT,"
			"func    TEXT,"
			"sender  TEXT,"
			"msg     TEXT"
		")"
	;
	sqlite3_stmt *stmt;
	rc = sqlite3_prepare_v2(db, create_stmt, sizeof(create_stmt), &stmt, NULL);
	if( rc != SQLITE_OK ) {
		snprintf(*errBuffer, ERR_MSG_LEN, "sqlite3_prepare_v2() failed: '%s'\n", sqlite3_errstr(rc));
    sqlite3_close(db);
    return -1;
	}
	rc = sqlite3_step(stmt);
	if( rc != SQLITE_DONE ) {
		snprintf(*errBuffer, ERR_MSG_LEN, "sqlite3_step() failed: '%s'\n", sqlite3_errstr(rc));
    sqlite3_close(db);
    return -1;
	}
	rc = sqlite3_finalize(stmt);
	if( rc ) {
		snprintf(*errBuffer, ERR_MSG_LEN,  "sqlite3_step() failed: '%s'\n", sqlite3_errstr(rc));
    sqlite3_close(db);
    return -1;
	}


	sql_logger_t *_logger = malloc(sizeof(sql_logger_t));
	if(!_logger)  {
		snprintf(*errBuffer, ERR_MSG_LEN,  "malloc() failed: '%s'\n",  strerror(errno));
    sqlite3_close(db);
    return -1;
	};


	_logger->db = db;
	_logger->print_to_console = true;
	*logger = _logger;

	free(*errBuffer);

	return 0;
}



void sql_logger_set_console_log(sql_logger_t *logger, bool to_console) {
	logger->print_to_console = to_console;
}

int sqlite_srv_logger(void *ctx, enum ducq_log_level level, const char *function_name, const char *sender_id, const char *fmt, va_list args) {
	sql_logger_t *logger = (sql_logger_t *)ctx;
	sqlite3 *db = logger->db;
	
	char now[] = "YYYY-MM-DDTHH-mm-SS";
	time_t time_val = time(NULL);
	struct tm *timeptr = localtime(&time_val);
	strftime(now, sizeof(now), "%FT%T", timeptr);

	va_list _args;
	va_copy(_args, args);
	char message[DUCQ_MSGSZ] = "";
	vsnprintf(message, DUCQ_MSGSZ, fmt, _args);
	va_end(_args);

	int rc = SQLITE_OK;

	const char create_stmt[] = "INSERT INTO monitor VALUES (?1, ?2, ?3, ?4, ?5);";
	
	sqlite3_stmt *stmt;
	rc = sqlite3_prepare_v2(db, create_stmt, sizeof(create_stmt), &stmt, NULL);
	if( rc != SQLITE_OK ) {
		fprintf(stderr, "sqlite3_prepare_v2() failed: '%s'\n", sqlite3_errstr(rc));
		syslog(LOG_ALERT, "sqlite3_prepare_v2() failed: '%s'\n", sqlite3_errstr(rc));
    return -1;
	}
	char *level_str = ducq_loglevel_tostring(level);
	sqlite3_bind_text(  stmt, 1, now,           strlen(now),           SQLITE_STATIC);
	sqlite3_bind_text(  stmt, 2, level_str,     strlen(level_str),     SQLITE_STATIC);
	sqlite3_bind_text(  stmt, 3, function_name, strlen(function_name), SQLITE_STATIC);
	sqlite3_bind_text(  stmt, 4, sender_id,     strlen(sender_id),     SQLITE_STATIC);
	sqlite3_bind_text(  stmt, 5, message,       strlen(message),       SQLITE_STATIC);
	rc = sqlite3_step(stmt);
	if( rc != SQLITE_DONE ) {
		fprintf(stderr, "sqlite3_step() failed: '%s'\n", sqlite3_errstr(rc));
		syslog(LOG_ALERT, "sqlite3_step() failed: '%s'\n", sqlite3_errstr(rc));
    return -1;
	}
	rc = sqlite3_finalize(stmt);
	if( rc ) {
		fprintf(stderr, "sqlite3_step() failed (%d): '%s'\n", rc, sqlite3_errstr(rc));
		syslog(LOG_ALERT, "sqlite3_step() failed (%d): '%s'\n", rc, sqlite3_errstr(rc));
    return -1;
	}



	if(logger->print_to_console) {
		va_copy(_args, args);
		ducq_color_console_log(NULL, level, function_name, sender_id, fmt, _args);
		va_end(_args);
	}

	return 0;
}


int free_sql_logger(sql_logger_t *logger) {
	sqlite3_close(logger->db);
	free(logger);
}
