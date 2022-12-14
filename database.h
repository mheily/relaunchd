/*
 * Copyright (c) 2018 Mark Heily <mark@heily.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _DATABASE_H
#define _DATABASE_H

#include <stdbool.h>
#include <stdint.h>
#include <sqlite3.h>

#define DB_OPEN_CREATE_VOLATILE 0x1
#define DB_OPEN_NO_VOLATILE 0x2
#define DB_OPEN_WITH_VIEWS 0x4

#define INVALID_ROW_ID ((int64_t) -12345)

#define db_error printlog(LOG_ERR, "database error %d: %s", sqlite3_errcode(dbh), sqlite3_errmsg(dbh))

struct string_array;

extern sqlite3 *dbh;

int db_init(void);
void db_shutdown(void);
int db_checkpoint(sqlite3 *conn);
int db_close(sqlite3 *conn);
int db_create(const char *, const char *);
int db_open(const char *, int);
int db_reopen();
bool db_exists(void);
int db_exec(sqlite3 *, const char *sql);
int db_exec_path(sqlite3 *, const char *);

int db_statement_bind(sqlite3_stmt *stmt, const char *fmt, va_list args);
int db_get_id(int64_t *result, const char *sql, const char *fmt, ...);
int db_get_string(char *dst, size_t sz, const char *sql, const char *fmt, ...);
int db_query(sqlite3_stmt **result, const char *sql, const char *fmt, ...);
int db_enable_tracing(void);

#endif /* _DATABASE_H */
