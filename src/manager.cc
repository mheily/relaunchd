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

#include "config.h"

#include <iostream>

#include "../vendor/FreeBSD/sys/queue.h"

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/wait.h>

#include "calendar.h"
#include "log.h"
#include "job.h"
#include "keepalive.h"
#include "manager.h"
#include "signal.h"
#include "socket.h"
#include "options.h"
#include "timer.h"
#include "util.h"
#include "channel.h"
#include "rpc_server.h"
#include "state_file.hpp"

using json = nlohmann::json;

static std::unique_ptr<StateFile> STATE_FILE;
static void setup_rpc_server();
static void setup_signal_handlers();
static void do_shutdown();

static LIST_HEAD(,job_manifest) pending; /* Jobs that have been submitted but not loaded */
static LIST_HEAD(,job) jobs;			/* All active jobs */

/* The kqueue descriptor used by main_loop() */
static int main_kqfd = -1;

/* The RPC server socket */
Channel chan;


void update_jobs(void)
{
	job_manifest_t jm, jm_tmp;
	job_t job, job_tmp;
	LIST_HEAD(,job) joblist;

	LIST_INIT(&joblist);

	/* Pass #1: load all jobs */
	LIST_FOREACH_SAFE(jm, &pending, jm_le, jm_tmp) {
		job = job_new(jm);
		if (!job)
			errx(1, "job_new()");

		/* Check for duplicate jobs */
		if (manager_get_job_by_label(jm->label)) {
			log_error("tried to load a duplicate job with label %s", jm->label);
			job_free(job);
			continue;
		}

		LIST_INSERT_HEAD(&joblist, job, joblist_entry);
		(void) job_load(job); // FIXME failure handling?
		log_debug("loaded job: %s", job->jm->label);
	}
	LIST_INIT(&pending);

	/* Pass #2: run all loaded jobs */
	LIST_FOREACH(job, &joblist, joblist_entry) {
		if (job_is_runnable(job)) {
			log_debug("running job %s from state %d", job->jm->label, job->state);
			(void) job_run(job); // FIXME failure handling?
		}
	}

	/* Pass #3: move all new jobs to the main jobs list */
	LIST_FOREACH_SAFE(job, &joblist, joblist_entry, job_tmp) {
		LIST_REMOVE(job, joblist_entry);
		LIST_INSERT_HEAD(&jobs, job, joblist_entry);
	}
}

int manager_wake_job(job_t job)
{
	if (job->state != JOB_STATE_WAITING) {
		log_error("tried to wake job %s that was not asleep (state=%d)",
				job->jm->label, job->state);
		return -1;
	}

	return job_run(job);
}

int manager_activate_job_by_fd(int fd)
{
    (void) fd;
	return -1; //STUB
}

job_t manager_get_job_by_label(const char *label)
{
	job_t job;

	LIST_FOREACH(job, &jobs, joblist_entry) {
		if (strcmp(label, job->jm->label) == 0) {
			return job;
		}
	}
	return NULL;
}

job_t manager_get_job_by_pid(pid_t pid)
{
	job_t job;

	LIST_FOREACH(job, &jobs, joblist_entry) {
		if (job->pid == pid) {
			return job;
		}
	}
	return NULL;
}

void manager_free_job(job_t job) {
	LIST_REMOVE(job, joblist_entry);
	job_free(job);
}

int manager_unload_job(const char *label)
{
	job_t job, job_tmp, job_cur;
	int retval = -1;
	char *path = NULL;

	job = NULL;
	LIST_FOREACH_SAFE(job_cur, &jobs, joblist_entry, job_tmp) {
		if (strcmp(job_cur->jm->label, label) == 0) {
			job = job_cur;
			break;
		}
	}

	if (!job) {
		log_error("job not found: %s", label);
		goto out;
	}

	if (job_unload(job) < 0) {
		goto out;
	}

	log_debug("job %s unloaded", label);

	if (job->state == JOB_STATE_DEFINED) {
		manager_free_job(job);
	}

	retval = 0;

out:
	free(path);
	return retval;
}

void
manager_unload_all_jobs()
{
	job_t job;
	job_t job_tmp;

	log_debug("unloading all jobs");
	LIST_FOREACH_SAFE(job, &jobs, joblist_entry, job_tmp) {
		if (job_unload(job) < 0) {
			log_error("job unload failed: %s", job->jm->label);
		} else {
			log_debug("job %s unloaded", job->jm->label);
		}
		manager_free_job(job);
	}
}

void manager_init() {
    LIST_INIT(&jobs);

    auto statedir = getStateDir();
    if (getuid() != 0 && !std::filesystem::exists(statedir)) {
        log_debug("creating %s", statedir.c_str());
        std::filesystem::create_directories(statedir);
    }
    STATE_FILE = std::make_unique<StateFile>(statedir + "/state.json", json::object());

    if ((main_kqfd = kqueue()) < 0)
        err(1, "kqueue(2)");
    setup_signal_handlers();
    setup_socket_activation(main_kqfd);
    setup_rpc_server();
    if (keepalive_init(main_kqfd) < 0)
        errx(1, "keepalive_init()");
    if (setup_timers(main_kqfd) < 0)
        errx(1, "setup_timers()");
    if (calendar_init(main_kqfd) < 0)
        errx(1, "calendar_init()");
}

void
manager_pid_event_add(int pid)
{
	struct kevent kev;

	EV_SET(&kev, pid, EVFILT_PROC, EV_ADD, NOTE_EXIT, 0, NULL);
	if (kevent(main_kqfd, &kev, 1, NULL, 0, NULL) < 0) {
		log_errno("kevent");
		//TODO: probably want to crash, or kill the job, or do something
		// more useful here.
	}
}

void
manager_pid_event_delete(int pid)
{
	struct kevent kev;

	/* This isn't necessary, I think, but just to be on the safe side.. */
	EV_SET(&kev, pid, EVFILT_PROC, EV_DELETE, NOTE_EXIT, 0, NULL);
	if (kevent(main_kqfd, &kev, 1, NULL, 0, NULL) < 0) {
		if (errno != ENOENT)
			err(1, "kevent");
	}
}

void 
manager_reap_child(pid_t pid, int status)
{
	job_t job;

// linux will need to do this in a loop after reading a signalfd and getting SIGCHLD
#if 0
	pid = waitpid(-1, &status, WNOHANG);
	if (pid < 0) {
		if (errno == ECHILD) return;
		err(1, "waitpid(2)");
	} else if (pid == 0) {
		return;
	}
#endif

	manager_pid_event_delete(pid);

	job = manager_get_job_by_pid(pid);
	if (!job) {
		log_error("child pid %d exited but no job found", pid);
		return;
	}

	if (job->state == JOB_STATE_KILLED) {
		/* The job is unloaded, so nobody cares about the exit status */
		manager_free_job(job);
		return;
	}

	if (job->jm->start_interval > 0) {
		job->state = JOB_STATE_WAITING;
	} else {
		job->state = JOB_STATE_EXITED;
	}
	if (WIFEXITED(status)) {
		job->last_exit_status = WEXITSTATUS(status);
	} else if (WIFSIGNALED(status)) {
		job->last_exit_status = -1;
		job->term_signal = WTERMSIG(status);
	} else {
		log_error("unhandled exit status");
	}
	log_debug("job %d exited with status %d", job->pid,
			job->last_exit_status);
	job->pid = 0;

	if (keepalive_add_job(job) < 0)
		log_error("keepalive_add_job()");

	return;
}

static void setup_signal_handlers()
{
	int i;
	struct kevent kev;

	for (i = 0; launchd_signals[i] != 0; i++) {
		if (signal(launchd_signals[i], SIG_IGN) == SIG_ERR)
			err(1, "signal(2): %d", launchd_signals[i]);
		EV_SET(&kev, launchd_signals[i], EVFILT_SIGNAL, EV_ADD, 0, 0,
               reinterpret_cast<void*>(&setup_signal_handlers));
		if (kevent(main_kqfd, &kev, 1, NULL, 0, NULL) < 0)
			err(1, "kevent(2)");
	}
}


static void setup_rpc_server()
{
    // FIXME: add try()
    chan.bindAndListen(getStateDir() + "/rpc.sock", 1024);
    chan.addEvent(main_kqfd, (void(*)(void *))&rpc_dispatch);
}

void
manager_main_loop()
{
	struct kevent kev;

	for (;;) {
		if (kevent(main_kqfd, NULL, 0, &kev, 1, NULL) < 1) {
			if (errno == EINTR) {
				continue;
			} else {
				err(1, "kevent(2)");
			}
		}
		/* TODO: refactor this to eliminate the use of switch() and just jump directly to the handler function */
		if ((void *)kev.udata == &setup_signal_handlers) {
			switch (kev.ident) {
			case SIGCHLD:
				/* NOTE: undocumented use of kev.data to obtain
				 * the status of the child. This should be reported to
				 * FreeBSD as a bug in the manpage.
				 */
				manager_reap_child(kev.ident, kev.data);
				break;
			case SIGINT:
				log_notice("caught SIGINT, exiting");
				manager_unload_all_jobs();
				exit(1);
				break;
			case SIGTERM:
				log_notice("caught SIGTERM, exiting");
				do_shutdown();
				exit(0);
				break;
			default:
				log_error("caught unexpected signal");
			}
		} else if (kev.filter == EVFILT_PROC) {
			(void) manager_reap_child(kev.ident, kev.data);
        } else if ((void *)kev.udata == &rpc_dispatch) {
            (void) rpc_dispatch(chan);
            // fixme: distinguish between a bad request and a fatal internal error
        } else if ((void *)kev.udata == &setup_socket_activation) {
			if (socket_activation_handler() < 0)
				errx(1, "socket_activation_handler()");
		} else if ((void *)kev.udata == &setup_timers) {
			if (timer_handler() < 0)
				errx(1, "timer_handler()");
		} else if ((void *)kev.udata == &calendar_init) {
			if (calendar_handler() < 0)
				errx(1, "calendar_handler()");
		} else if ((void *)kev.udata == &keepalive_wake_handler) {
			keepalive_wake_handler();
		} else {
			log_warning("spurious wakeup, no known handlers");
		}
	}
}

int manager_load_manifest(const std::filesystem::path &path) {
    job_manifest_t jm = NULL;

    jm = job_manifest_new();
    if (!jm) {
        log_error("job_manifest_new()");
        return -1;
    }

    log_debug("loading %s", path.c_str());
    if (job_manifest_read(jm, path.c_str()) < 0) {
        log_error("parse error");
        job_manifest_free(jm);
        return -1;
    }

    log_debug("defined job: %s", jm->label);

    /* Check for duplicate jobs */
    if (manager_get_job_by_label(jm->label)) {
        log_error("tried to load a duplicate job with label %s", jm->label);
        job_manifest_free(jm);
        return -1;
    }
    job_t job = job_new(jm);
    if (!job) {
        job_manifest_free(jm);
        return -1;
    }

    (void) job_load(job); // FIXME failure handling?
    log_debug("loaded job: %s", job->jm->label);

    LIST_INSERT_HEAD(&jobs, job, joblist_entry);

    if (job_is_runnable(job)) {
        log_debug("running job %s from state %d", job->jm->label, job->state);
        (void) job_run(job); // FIXME failure handling?
    }

    return 0;
}

json manager_list_jobs() {
    job_t job;
    auto result = json::array();
    LIST_FOREACH(job, &jobs, joblist_entry) {
        std::string pid = (job->pid == 0) ? "-" : std::to_string(job->pid);
        if (job->pid == 0) {
            pid = "-";
        } else {
            pid = std::to_string(job->pid);
        }
        result.emplace_back(
                json::object(
                        {
                                {"Label",          std::string{job->jm->label}},
                                {"PID",            std::move(pid)},
                                {"LastExitStatus", job->last_exit_status},
                        }
                ));
    }
    return result;
}

static void do_shutdown()
{
}

