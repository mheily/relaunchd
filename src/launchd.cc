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
#include <fcntl.h>

#if defined(__linux__)
#include <sys/prctl.h>
#endif

#include "config.h"
#include "log.h"
#include "manager.h"

void double_fork(void);

void redirect_stdio(pid_t);

int become_a_subreaper();

int become_a_subreaper() {
#if defined(__FreeBSD__)
    if (procctl(P_PID, getpid(), PROC_REAP_ACQUIRE, 0) < 0) {
            log_errno("procctl(2)");
    }
    return 0;
#elif defined(__linux__)
    if (prctl(PR_SET_CHILD_SUBREAPER, 1) < 0) {
            log_errno("prctl(2)");
    }
    return 0;
#else
    log_debug("this platform does not support becoming a subreaper");
    return -1;
#endif
}

void double_fork(void) {
    for (int i = 0; i < 2; i++) {
        switch (fork()) {
            case -1:
                log_errno("fork(2)");
                abort();
            case 0:
                break;
            default:
                _exit(0);
        }
    }
}

void redirect_stdio(pid_t pid) {
    int fd = open("/dev/null", O_RDWR, 0);
    if (fd < 0) {
        log_errno("unable to open /dev/null");
        abort();
    } else {
        // todo: check if dup2 fails
        if (pid == 1) {
            // Special case: stdio has not been fully initialized
            (void) dup2(fd, 3);
            fd = 3;
        }
        (void) dup2(fd, STDIN_FILENO);
        (void) dup2(fd, STDOUT_FILENO);
        (void) dup2(fd, STDERR_FILENO);
        (void) close(fd);
    }
}

void usage() {
    printf("todo: usage\n");
}

int main(int argc, char *argv[]) {
    int c;
    pid_t pid = getpid();
    bool daemonize = (pid != 1);
    int logmask = LOG_NOTICE;

//    /* Sanitize environment variables */
//    if ((getuid() != 0) && (access(getenv("HOME"), R_OK | W_OK | X_OK) < 0)) {
//        fputs("Invalid value for the HOME environment variable\n", stderr);
//        exit(1);
//    }

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

    // FIXME: pid 1 logging cannot go to syslogd because of chicken+egg
    openlog("launchd", LOG_PID | LOG_NDELAY, LOG_DAEMON);
    setlogmask(logmask);
    log_notice("relaunchd version %s starting", relaunch::config::VERSION);

    if (daemonize) {
        if (chdir("/") != 0) {
            abort();
        }
        if (pid != 1) {
            double_fork();
        }
        redirect_stdio(pid);
    } else {
        log_freopen(stdout);
    }

    (void) become_a_subreaper();

    DomainType domain;
    if (getuid() == 0) {
        domain = DOMAIN_TYPE_SYSTEM;
    } else {
        // XXX-FIXME probably need to implement "launchctl bootstrap"
        // to initialize these correctly.
        if (getenv("DISPLAY")) {
            domain = DOMAIN_TYPE_GUI;
        } else {
            domain = DOMAIN_TYPE_USER;
        }
    }

    Manager mgr{domain};
    mgr.loadDefaultManifests();
    mgr.startAllJobs();

    while (mgr.handleEvent()) {}

    exit(EXIT_SUCCESS);
}
