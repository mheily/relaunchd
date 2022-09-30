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

#include <fstream>
#include <iostream>
#include <unordered_map>

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

static std::unordered_map<std::string, Job> services;

/* The kqueue descriptor used by main_loop() */
static int main_kqfd = -1;

/* The RPC server socket */
Channel chan;




int manager_wake_job(Job &job)
{
	if (job.state != JOB_STATE_WAITING) {
		log_error("tried to wake job %s that was not asleep (state=%d)",
				job.manifest.label.c_str(), job.state);
		return -1;
	}

    job.run();
    return 0;
}

int manager_activate_job_by_fd(int fd)
{
    (void) fd;
	return -1; //STUB
}

Job & manager_get_job_by_label(const std::string &label)
{
    auto it = services.find(label);
    if (it != services.end()) {
        return it->second;
    } else {
        throw std::range_error(label);
    }
}

Job & manager_get_job_by_pid(pid_t pid)
{
    for (auto & [label, job] : services) {
        if (job.pid == pid) {
            return job;
        }
    }
    throw std::range_error(std::to_string(pid));
}

int manager_unload_job(const char *label)
{
    auto it = services.find(label);
    if (it == services.end()) {
        log_error("job not found: %s", label);
        return -1;
    }
    auto job = it->second;

    job.unload();

	log_debug("job %s unloaded", label);

    services.erase(it);

    return 0;
}

void
manager_unload_all_jobs()
{
	log_debug("unloading all jobs");
    auto it = services.begin();
    while (it != services.end()) {
        auto job = it->second;
        try {
            job.unload();
        } catch (...) {
            log_error("failed to unload %s: ignoring because all jobs are being unloaded",
                      job.manifest.label.c_str());
        }
        it = services.erase(it);
    }
}

void manager_init() {
    auto statedir = getStateDir();
    if (getuid() != 0 && !std::filesystem::exists(statedir)) {
        log_debug("creating %s", statedir.c_str());
        std::filesystem::create_directories(statedir);
    }

    json defaultStateDoc = {
            {"SchemaVersion", 1},
            {"Overrides", json::object()}
    };
    STATE_FILE = std::make_unique<StateFile>(statedir + "/state.json", defaultStateDoc);

    if ((main_kqfd = kqueue()) < 0)
        err(1, "kqueue(2)");
    setup_signal_handlers();
    //setup_socket_activation(main_kqfd);
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

    // fixme: check for range error exception
	auto job = manager_get_job_by_pid(pid);
//	if (!job) {
//		log_error("child pid %d exited but no job found", pid);
//		return;
//	}

	if (job.state == JOB_STATE_KILLED) {
		/* The job is unloaded, so nobody cares about the exit status */
		return;
	}

	if (job.manifest.start_interval > 0) {
		job.state = JOB_STATE_WAITING;
	} else {
		job.state = JOB_STATE_EXITED;
	}
	if (WIFEXITED(status)) {
		job.last_exit_status = WEXITSTATUS(status);
	} else if (WIFSIGNALED(status)) {
		job.last_exit_status = -1;
		job.term_signal = WTERMSIG(status);
	} else {
		log_error("unhandled exit status");
	}
	log_debug("job %d exited with status %d", job.pid,
			job.last_exit_status);
	job.pid = 0;

    if (job.manifest.keep_alive.always) {
        keepalive_add_job(job);
    }
    // FIXME: what about calendar and timer jobs?
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

			case SIGTERM:
				log_notice("caught SIGTERM, exiting");
				do_shutdown();
				exit(0);

            case SIGPIPE:
                break;

			default:
				log_error("caught unexpected signal");
			}
		} else if (kev.filter == EVFILT_PROC) {
			(void) manager_reap_child(kev.ident, kev.data);
        } else if ((void *)kev.udata == &rpc_dispatch) {
            (void) rpc_dispatch(chan);
            // fixme: distinguish between a bad request and a fatal internal error
//        } else if ((void *)kev.udata == &setup_socket_activation) {
//			if (socket_activation_handler() < 0)
//				errx(1, "socket_activation_handler()");
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

int manager_load_manifest(const json &manifest, const std::string &path) {
    std::string label;
    if (manifest.contains("Label")) {
        manifest.at("Label").get_to(label);
    } else {
        log_error("manifest has no Label key");
        return -1;
    }

    /* Check for duplicate jobs */
    if (services.count(label)) {
        log_error("tried to load a duplicate job with label %s", label.c_str());
        return -1;
    }

    // Check if the job is disabled
    auto state = STATE_FILE->getValue();
    if (state.at("Overrides").contains(label)) {
        const auto job_state = state["Overrides"][label];
        if (!job_state.at("Enabled")) {
            log_debug("tried to load %s but it is disabled via local override", label.c_str());
            return 0;
        }
    }

    services.insert({label, Job{path, manifest.get<manifest::Manifest>()}});
    auto & job = services.at(label);

    log_debug("defined job: %s", label.c_str());

    job.load();
    log_debug("loaded job: %s", job.manifest.label.c_str());

    if (job.manifest.keep_alive.always || job.manifest.run_at_load) {
        job.run();
    }

    return 0;
}

int manager_load_manifest(const std::filesystem::path &path) {
    log_debug("loading %s", path.c_str());
    json obj = manifest::parse(path);
    return manager_load_manifest(obj, path);
}

int manager_unload_by_label(const std::string &label) {
    if (!services.count(label)) {
        log_info("tried to unload a job that is not loaded: %s", label.c_str());
        return -1;
    }
    auto & job = services.at(label);
    job.unload();
    log_debug("unloaded job: %s", job.manifest.label.c_str());
    services.erase(label);
    return 0;
}

int manager_unload_manifest(const std::filesystem::path &path) {
    log_debug("unloading %s", path.c_str());
    json obj = manifest::parse(path);

    std::string label;
    if (obj.contains("Label")) {
        obj.at("Label").get_to(label);
    } else {
        log_error("manifest has no Label key");
        return -1;
    }

    return manager_unload_by_label(label);
}

json manager_list_jobs() {
    auto result = json::array();
    for (const auto & [label, job] : services) {
        std::string pid = (job.pid == 0) ? "-" : std::to_string(job.pid);
        if (job.pid == 0) {
            pid = "-";
        } else {
            pid = std::to_string(job.pid);
        }
        result.emplace_back(
                json::object(
                        {
                                {"Label",          std::string{job.manifest.label}},
                                {"PID",            std::move(pid)},
                                {"LastExitStatus", job.last_exit_status},
                        }
                ));
    }
    return result;
}

void manager_set_job_enabled(const std::string &label, bool enabled) {
    auto job = manager_get_job_by_label(label);
    auto doc = STATE_FILE->getValue();
    if (doc.at("Overrides").contains(label)) {
        doc["Overrides"][label]["Enabled"] = enabled;
    } else {
        doc["Overrides"].emplace(label, json::object({{"Enabled", enabled}}));
    }
    STATE_FILE->setValue(doc);
}

static void do_shutdown()
{
}

