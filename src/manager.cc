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
#include <sstream>

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


int manager_activate_job_by_fd(int fd)
{
    (void) fd;
	return -1; //STUB
}


void Manager::reapChildProcess(pid_t pid, int status) {
    // FIXME: check for range error exception because we are a subreaper on some platforms
    // See: https://github.com/mheily/relaunchd/issues/15
    auto maybe_job = at(pid);
    if (!maybe_job) {
		log_debug("child pid %d exited but no matching job found", pid);
		return;
	}
    auto job = *maybe_job;

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
            startJob(job);
        } else {
            time_t delta = restart_at - now;
            if (delta > INT_MAX || delta < 0) {
                throw std::range_error("interval too large");
            }
            int seconds = static_cast<int>(delta);
            log_debug("%s: will restart in %d seconds due to KeepAlive setting", job->manifest.label.c_str(), seconds);
            job->state = JOB_STATE_WAITING;
            eventmgr.addTimer( seconds, [job, this]() {
                // The job may have been unloaded in the interval, or manually started by an administrator.
                if (job->state == JOB_STATE_WAITING) {
                    startJob(job);
                } else {
                    log_debug("attempted to restart job %s: not waiting to be started", job->manifest.label.c_str());
                }
            });
        }
    }

    // FIXME: what about calendar and timer jobs?
}

void Manager::setupSignalHandlers() {
    // FIXME testing eventmgr.addSignal(SIGCHLD, [](int){});

    eventmgr.addSignal(SIGPIPE, [](int){});

    eventmgr.addSignal(SIGINT, [this](int){
        SHUTTING_DOWN = true;
        log_notice("caught SIGINT, exiting");
    });

    eventmgr.addSignal(SIGTERM, [this](int){
        SHUTTING_DOWN = true;
        log_notice("caught SIGTERM, exiting");
    });
}


bool Manager::loadManifest(const std::filesystem::path &path, bool overrideDisabled, bool forceLoad) {
    json obj = manifest::parse(path);
    return loadManifest(obj, path, overrideDisabled, forceLoad);
}

bool
Manager::loadManifest(const json &jsondata, const std::string &path, bool overrideDisabled, bool forceLoad) {
    Manifest manifest;
    try {
        manifest = jsondata.get<manifest::Manifest>();
    } catch (const std::exception &exc) {
        log_error("failed to parse manifest at %s: %s", path.c_str(), exc.what());
        return false;
    }

    /* Check for duplicate jobs */
    if (services.count(manifest.label)) {
        log_error("tried to load a duplicate job with label %s", manifest.label.c_str());
        return false;
    }

    if (overrideDisabled) {
        log_debug("%s: overriding the Disabled key", manifest.label.c_str());
        overrideJobEnabled(manifest.label, true);
    }

    // Check if the job is disabled
    if (manifest.disabled && !forceLoad) {
        auto state = STATE_FILE->getValue();
        if (state.at("Overrides").contains(manifest.label)) {
            const auto job_state = state["Overrides"][manifest.label];
            if (job_state.at("Enabled")) {
                forceLoad = true;
            }
        }
    }

    if (manifest.disabled && !forceLoad) {
        log_debug("will not load %s: it is disabled", manifest.label.c_str());
        return false;
    }

    std::shared_ptr<Job> job = std::make_shared<Job>(path, manifest);
    services.emplace(manifest.label,job);

    job->load();

    return true;
}

int Manager::unloadJob(const std::string &label, bool overrideDisabled, bool forceUnload) {
    if (!services.count(label)) {
        log_info("tried to unload a job that is not loaded: %s", label.c_str());
        return -1;
    }
    if (overrideDisabled) {
        log_debug("%s: overriding the Disabled key", label.c_str());
        overrideJobEnabled(label, false);
    }
    auto & job = services.at(label);

    // Check if the job is disabled
    if (!job->manifest.disabled && !forceUnload) {
        auto state = STATE_FILE->getValue();
        if (state.at("Overrides").contains(label)) {
            const auto job_state = state["Overrides"][label];
            if (!job_state.at("Enabled")) {
                forceUnload = true;
            }
        }
    }

    if (job->manifest.disabled && !forceUnload) {
        log_debug("will not unload %s: it is disabled", label.c_str());
        return 0;
    }

    job->unload();
    log_debug("unloaded job: %s", job->manifest.label.c_str());
    services.erase(label);
    return 0;
}

int Manager::unloadJob(const std::filesystem::path &path, bool overrideDisabled, bool forceUnload) {
    json obj = manifest::parse(path);
    std::string label;
    if (obj.contains("Label")) {
        obj.at("Label").get_to(label);
    } else {
        log_error("manifest has no Label key");
        return -1;
    }
    return unloadJob(label, overrideDisabled, forceUnload);
}

json Manager::listJobs() {
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

void Manager::overrideJobEnabled(const std::string &label, bool enabled) {
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

Manager::Manager(DomainType domain_) : domain(domain_) {
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

    setupSignalHandlers();
    //setup_socket_activation(main_kqfd);

    // Set up the RPC server
    chan.bindAndListen(getStateDir() + "/rpc.sock", 1024);
    eventmgr.addSocketRead(chan.getSockFD(), [this](int) { rpc_dispatch(chan, *this); });
}

Manager::~Manager() {
    log_debug("manager shutting down");
    clear();
}

bool Manager::handleEvent() {
    log_debug("waiting for an event");
    try {
        eventmgr.waitForEvent();
    } catch (const std::exception &e) {
        log_error("caught exception: %s", e.what());
    }
    return !SHUTTING_DOWN;
}

std::optional<std::shared_ptr<Job>> Manager::at(const pid_t pid) const {
    // TODO: switch to running_jobs
//    auto it = services.find(pid);
//    if (it != services.end()) {
//        return it->second;
//    } else {
//        return std::nullopt;
//    }
    for (const auto & [label, job] : services) {
        if (job->pid == pid) {
            return job;
        }
    }
    return std::nullopt;
}

std::optional<std::shared_ptr<Job>> Manager::at(const std::string &label) const {
    // TODO: switch to loaded_jobs
    auto it = services.find(label);
    if (it != services.end()) {
        return it->second;
    } else {
        return std::nullopt;
    }
}

int Manager::erase(const std::string &label) {
    // XXX FIXME kill and remove from running jobs

    auto it = services.find(label);
    if (it == services.end()) {
        return 0;
    }
    auto & job = it->second;

    job->unload();

    log_debug("job %s unloaded", label.c_str());

    return 0;
}

void Manager::wakeJob(std::shared_ptr<Job> &job) {
    if (job->state != JOB_STATE_WAITING) {
        log_error("tried to wake job %s that was not asleep (state=%d)",
                  job->manifest.label.c_str(), job->state);
        throw std::logic_error("job in wrong state");
    }
    startJob(job);
}

void Manager::clear() {
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

void Manager::rescheduleCalendarJob(const std::shared_ptr<Job> &job) {
    auto maybe_schedule = calendar::schedule_calendar_job(
            job->manifest.calendar_interval.value());
    if (!maybe_schedule) {
        return;
    }
    auto [absolute_time, relative_time] = maybe_schedule.value();
    log_debug("job %s scheduled to run in %d minutes at t=%ld", job->manifest.label.c_str(), relative_time,
              absolute_time);
    job->state = JOB_STATE_WAITING;
    eventmgr.addTimer(relative_time, [job, this]() {
        // The job may have been unloaded in the interval, or manually started by an administrator.
        if (job->state == JOB_STATE_WAITING) {
            startJob(job);
            rescheduleCalendarJob(job);
        }
    });
    // XXX-FIXME: update StateFile to set the absolute start time.
}

void Manager::reschedulePeriodicJob(const std::shared_ptr<Job> &job) {
    log_debug("job %s will start after T=%u", job->manifest.label.c_str(), job->manifest.start_interval);
    job->state = JOB_STATE_WAITING;
    eventmgr.addTimer(job->manifest.start_interval, [&job, this]() {
        // The job may have been unloaded in the interval, or manually started by an administrator.
        if (job->state == JOB_STATE_WAITING) {
            startJob(job);
            reschedulePeriodicJob(job);
        }
    });
}

void Manager::rescheduleJob(const std::shared_ptr<Job> &job) {
    switch (job->schedule) {
        case JOB_SCHEDULE_PERIODIC:
            reschedulePeriodicJob(job);
            break;
        case JOB_SCHEDULE_CALENDAR:
            rescheduleCalendarJob(job);
            break;
        default:
            throw std::logic_error("wrong job type");
    }
}

void Manager::startJob(const std::shared_ptr<Job> &job) {
    job->run();
    eventmgr.addProcess(job->pid, [this](pid_t pid, int status) {
        reapChildProcess(pid, status);
    });
    job->state = JOB_STATE_RUNNING;
}

void Manager::startAllJobs() {
    for (const auto & [label, job] : services) {
        if (job->state == JOB_STATE_LOADED) {
            if (job->manifest.keep_alive.always || job->manifest.run_at_load) {
                startJob(job);
            }
            if (job->schedule != JOB_SCHEDULE_NONE) {
                rescheduleJob(job);
            }
        }
    }
}


void Manager::loadDefaultManifests() {
    switch (domain) {
        case DOMAIN_TYPE_SYSTEM:
            (void)loadAllManifests(VENDOR_DAEMON_LOAD_PATH);
            (void)loadAllManifests(SYSTEM_DAEMON_LOAD_PATH);
            break;
        case DOMAIN_TYPE_USER:
            (void)loadAllManifests(USER_DAEMON_LOAD_PATH);
            break;
        case DOMAIN_TYPE_GUI:
            // TODO: implement LaunchAgents
            throw std::runtime_error("not supported");
            break;
    }
}

bool Manager::loadAllManifests(const std::string &load_path, bool overrideDisabled, bool forceLoad) {
    std::vector<std::string> paths;
    std::stringstream ss(load_path);
    while (ss.good()) {
        std::string str;
        std::getline(ss, str, ':');

        // Replace $HOME with the actual HOME
        if (getenv("HOME")) {
            auto index = str.find("$HOME");
            if (index != std::string::npos) {
                str.replace(index, 5, getenv("HOME"));
            }
        }

        paths.emplace_back(str);
    }

    bool error = false;
    for (const auto &path : paths) {
        if (!std::filesystem::exists(path)) {
            log_warning("load failed: path does not exist: %s", path.c_str());
            error = true;
            continue;
        }
        if (std::filesystem::is_directory(path)) {
            log_notice("loading all manifests in %s/", path.c_str());
            using std::filesystem::directory_iterator;
            for (const auto &file: directory_iterator(path)) {
                try {
                    if (!loadManifest(file.path(), overrideDisabled, forceLoad)) {
                        error = true;
                    }
                } catch (...) {
                    log_error("unhandled exception while parsing %s", file.path().c_str());
                    error = true;
                }
            }
        } else {
            try {
                std::filesystem::path p{path};
                if (!loadManifest(p, overrideDisabled, forceLoad)) {
                    error = true;
                }
            } catch (...) {
                log_error("unhandled exception while parsing %s", path.c_str());
                error = true;
            }
        }
    }
    return error;
}

