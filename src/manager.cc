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
#include <sys/wait.h>

#include "calendar.h"
#include "event.h"
#include "log.h"
#include "job.h"
#include "manager.h"
#include "options.h"
#include "channel.h"
#include "rpc_server.h"
#include "state_file.hpp"
#include "clock.h"

using json = nlohmann::json;

static std::unique_ptr<StateFile> STATE_FILE;

static void setup_signal_handlers();
static void manager_reap_child(pid_t pid, int status);

static std::unordered_map<std::string, std::shared_ptr<Job>> services;

/* The kqueue descriptor used by main_loop() */
//static int main_kqfd = -1;
static kq::EventManager eventmgr;

/* The RPC server socket */
Channel chan;

static bool SHUTTING_DOWN = false;



int manager_wake_job(std::shared_ptr<Job> job)
{
	if (job->state != JOB_STATE_WAITING) {
		log_error("tried to wake job %s that was not asleep (state=%d)",
				job->manifest.label.c_str(), job->state);
		return -1;
	}

    job->run();
    return 0;
}

int manager_activate_job_by_fd(int fd)
{
    (void) fd;
	return -1; //STUB
}

std::shared_ptr<Job> manager_get_job_by_label(const std::string &label)
{
    auto it = services.find(label);
    if (it != services.end()) {
        return it->second;
    } else {
        throw std::range_error(label);
    }
}

std::shared_ptr<Job> manager_get_job_by_pid(pid_t pid)
{
    for (auto & [label, job] : services) {
        if (job->pid == pid) {
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
    auto & job = it->second;

    job->unload();

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
        auto & job = it->second;
        try {
            job->unload();
        } catch (...) {
            log_error("failed to unload %s: ignoring because all jobs are being unloaded",
                      job->manifest.label.c_str());
        }
        it = services.erase(it);
    }
}

void manager_init() noexcept {
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

    setup_signal_handlers();
    //setup_socket_activation(main_kqfd);

    // Set up the RPC server
    chan.bindAndListen(getStateDir() + "/rpc.sock", 1024);
    eventmgr.addSocketRead(chan.getSockFD(), [](int) { rpc_dispatch(chan); });
}

void
manager_pid_event_add(int pid)
{
    eventmgr.addProcess(pid, manager_reap_child);
}

static void reschedule_calendar_job(std::shared_ptr<Job> &job) {
    auto maybe_schedule = calendar::schedule_calendar_job(
            job->manifest.calendar_interval.value());
    if (!maybe_schedule) {
        return;
    }
    auto [absolute_time, relative_time] = maybe_schedule.value();
    log_debug("job %s scheduled to run in %d minutes at t=%ld", job->manifest.label.c_str(), relative_time,
              absolute_time);
    job->state = JOB_STATE_WAITING;
    eventmgr.addTimer(relative_time, [&job]() {
        // The job may have been unloaded in the interval, or manually started by an administrator.
        if (job->state == JOB_STATE_WAITING) {
            job->run();
            reschedule_calendar_job(job);
        }
    });
    // XXX-FIXME: update StateFile to set the absolute start time.
}

static void reschedule_periodic_job(std::shared_ptr<Job> &job) {
    log_debug("job %s will start after T=%u", job->manifest.label.c_str(), job->manifest.start_interval);
    job->state = JOB_STATE_WAITING;
    eventmgr.addTimer(job->manifest.start_interval, [&job]() {
        // The job may have been unloaded in the interval, or manually started by an administrator.
        if (job->state == JOB_STATE_WAITING) {
            job->run();
            reschedule_periodic_job(job);
        }
    });
}

static void manager_reschedule_job(std::shared_ptr<Job> &job) {
    switch (job->schedule) {
        case JOB_SCHEDULE_PERIODIC:
            reschedule_periodic_job(job);
            break;
        case JOB_SCHEDULE_CALENDAR:
            reschedule_calendar_job(job);
            break;
        default:
            throw std::logic_error("wrong job type");
    }
}

static void
manager_reap_child(pid_t pid, int status)
{
    // FIXME: check for range error exception because we are a subreaper on some platforms
    // See: https://github.com/mheily/relaunchd/issues/15
    auto job = manager_get_job_by_pid(pid);
//	if (!job) {
//		log_error("child pid %d exited but no job found", pid);
//		return;
//	}

	if (job->state == JOB_STATE_KILLED) {
		/* The job is unloaded, so nobody cares about the exit status */
		return;
	}

	if (job->manifest.start_interval > 0) {
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

    if (job->manifest.keep_alive.always) {
        time_t restart_at = job->started_at + job->manifest.throttle_interval;
        time_t now = current_time();
        if (now >= restart_at) {
            log_debug("%s: restarting due to KeepAlive", job->manifest.label.c_str());
            job->run();
        } else {
            time_t delta = restart_at - now;
            if (delta > INT_MAX || delta < 0) {
                throw std::range_error("interval too large");
            }
            int seconds = static_cast<int>(delta);
            log_debug("%s: will restart in %d seconds due to KeepAlive setting", job->manifest.label.c_str(), seconds);
            job->state = JOB_STATE_WAITING;
            eventmgr.addTimer( seconds, [job]() {
                // The job may have been unloaded in the interval, or manually started by an administrator.
                if (job->state == JOB_STATE_WAITING) {
                    job->run();
                }
            });
        }
    }

    // FIXME: what about calendar and timer jobs?
}

static void setup_signal_handlers() {
    // FIXME testing eventmgr.addSignal(SIGCHLD, [](int){});

    eventmgr.addSignal(SIGPIPE, [](int){});

    eventmgr.addSignal(SIGINT, [](int){
        SHUTTING_DOWN = true;
        log_notice("caught SIGINT, exiting");
    });

    eventmgr.addSignal(SIGTERM, [](int){
        SHUTTING_DOWN = true;
        log_notice("caught SIGTERM, exiting");
    });
}

bool manager_handle_event() {
    log_debug("waiting for an event");
    try {
        eventmgr.waitForEvent();
    } catch (...) {
        log_error("caught exception"); // todo: print what()
    }
    return !SHUTTING_DOWN;
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


    std::shared_ptr<Job> job = std::make_shared<Job>(path, manifest.get<manifest::Manifest>());
    services.emplace(label,job);

    log_debug("defined job: %s", label.c_str());

    job->load();
    log_debug("loaded job: %s", job->manifest.label.c_str());

    if (job->manifest.keep_alive.always || job->manifest.run_at_load) {
        job->run();
    }

    if (job->schedule != JOB_SCHEDULE_NONE) {
        manager_reschedule_job(job);
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
    job->unload();
    log_debug("unloaded job: %s", job->manifest.label.c_str());
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
        std::string pid = (job->pid == 0) ? "-" : std::to_string(job->pid);
        if (job->pid == 0) {
            pid = "-";
        } else {
            pid = std::to_string(job->pid);
        }
        result.emplace_back(
                json::object(
                        {
                                {"Label",          std::string{job->manifest.label}},
                                {"PID",            std::move(pid)},
                                {"LastExitStatus", job->last_exit_status},
                        }
                ));
    }
    return result;
}

void manager_set_job_enabled(const std::string &label, bool enabled) {
    //FIXME: do we care if it exists?
    // auto & job = manager_get_job_by_label(label);
    auto doc = STATE_FILE->getValue();
    if (doc.at("Overrides").contains(label)) {
        doc["Overrides"][label]["Enabled"] = enabled;
    } else {
        doc["Overrides"].emplace(label, json::object({{"Enabled", enabled}}));
    }
    STATE_FILE->setValue(doc);
}

void manager_shutdown() {
    log_debug("manager shutting down");
    manager_unload_all_jobs();
}

