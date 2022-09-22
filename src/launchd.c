/*
 * Copyright (c) 2015 Mark Heily <mark@heily.com>
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


#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <syslog.h>
#include <unistd.h>

#include "log.h"
#include "manager.h"

void usage() 
{
	printf("todo: usage\n");
}

#ifndef UNIT_TEST
int
main(int argc, char *argv[])
{
	int c;

	/* Sanitize environment variables */
	if ((getuid() != 0) && (access(getenv("HOME"), R_OK | W_OK | X_OK) < 0)) {
		fputs("Invalid value for the HOME environment variable\n", stderr);
		exit(1);
	}

    bool daemonize = true;
	int logmask = LOG_NOTICE;

	while ((c = getopt(argc, argv, "fv")) != -1) {
			switch (c) {
			case 'f':
					daemonize = false;
					break;
			case 'v':
					logmask = LOG_DEBUG;
					break;
			default:
					usage();
					break;
			}
	}

    openlog("launchd", LOG_PID | LOG_NDELAY, LOG_DAEMON);
    setlogmask(logmask);

/* daemon(3) is deprecated on MacOS */
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

	if (daemonize && daemon(0, 0) < 0) {
		fprintf(stderr, "ERROR: Unable to daemonize\n");
		exit(EX_OSERR);
	} else {
		log_freopen(stdout);
	}

#ifdef __clang__
#pragma clang diagnostic pop
#endif

	manager_init();
	manager_main_loop();

	/* NOTREACHED */
	exit(EXIT_SUCCESS);
}
#endif /* !UNIT_TEST */
