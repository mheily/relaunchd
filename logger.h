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

#ifndef _LOGGER_H
#define _LOGGER_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#define printlog(level, format,...) logger_append(level, "%s(%s:%d): "format"\n", __func__, strrchr(__FILE__, '/') + 1, __LINE__, ## __VA_ARGS__)

extern FILE *logger_fh;
extern int logger_use_syslog;

FILE *logger_fh;

int logger_add_file_appender(const char *path);
int logger_add_stderr_appender(void);
int logger_add_syslog_appender(const char *ident, int option, int facility);
int logger_init(void);
void logger_shutdown(void);
int logger_open(const char *);
int __attribute__((format(printf, 2, 3))) logger_append(int level, const char *format, ...);
void logger_set_verbose(int);
int logger_redirect_file_descriptor(int oldfd, const char *path, int flags, mode_t mode);

#endif /* _LOGGER_H */
