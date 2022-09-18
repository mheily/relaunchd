/*
 * Copyright (c) 2016 Mark Heily <mark@heily.com>
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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "config.h"

struct job;

/**
 * Logic around automatically restarting jobs.
 */

#define KEEPALIVEDIR VARDIR "/db/launchd/cfg"

/** One-time initialization at program startup */
int keepalive_init(int kqfd);

/** Check if a job should be restarted after it exists. */
int keepalive_add_job(struct job *job);

/** Remove any watchdogs associated with a job  */
void keepalive_remove_job(struct job *job);

/** Handle the wakeup event, possibly restarting jobs */
void keepalive_wake_handler(void);

#ifdef __cplusplus
}
#endif
