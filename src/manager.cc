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
#include <queue>
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

std::optional<Label> Manager::reapChildProcess(pid_t pid, int status) {
    auto maybe_label = getLabelByPid(pid);
    if (!maybe_label) {
        // This can happen if we are a subreaper and a child exits after their parent.
        log_debug("child pid %d exited but no matching job found", pid);
        return std::nullopt;
    }
    const std::string &label = maybe_label.value();
    auto &job = getJob(label);
    // TODO: Set this:
    //      job.exit_status = status
    //  and get rid of:
    //      job.last_exit_status
    //      job.term_signal
    //  converting the above into methods that examine exit_status.
    if (WIFEXITED(status)) {
		job.last_exit_status = WEXITSTATUS(status);
	} else if (WIFSIGNALED(status)) {
		job.last_exit_status = -1;
		job.term_signal = WTERMSIG(status);
    } else if (WIFSTOPPED(status)) {
        job.last_exit_status = -1;
        job.term_signal = -1;
    } else {
        throw std::range_error("invalid status");
	}
    log_debug("job %s pid %d exited with status %d", job.manifest.label.c_str(),
              job.pid,
              job.last_exit_status);
	job.pid = 0;
    job.state = JOB_STATE_EXITED;
    return label;
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
    if (jobs.count(manifest.label)) {
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

    auto [it, _] = jobs.emplace(std::string{manifest.label}, Job{path, manifest});
    auto &job = it->second;
    job.load();

    return true;
}


void Manager::unloadJob(Job &job, bool overrideDisabled, bool forceUnload) {
    std::string label = job.manifest.label;

    if (overrideDisabled) {
        log_debug("%s: overriding the Disabled key", label.c_str());
        overrideJobEnabled(label, false);
    }

    // Check if the job is disabled
    if (!job.manifest.disabled && !forceUnload) {
        auto state = STATE_FILE->getValue();
        if (state.at("Overrides").contains(label)) {
            const auto job_state = state["Overrides"][label];
            if (!job_state.at("Enabled")) {
                forceUnload = true;
            }
        }
    }

    if (job.manifest.disabled && !forceUnload) {
        log_debug("will not unload %s: it is disabled", label.c_str());
        return;
    }

    job.unload();
}

int Manager::unloadJob(const std::string &label, bool overrideDisabled, bool forceUnload) {
    if (!jobs.count(label)) {
        log_info("tried to unload a job that is not loaded: %s", label.c_str());
        return -1;
    }
    auto & job = jobs.at(label);
    unloadJob(job, overrideDisabled, forceUnload);
    jobs.erase(label);
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
    unloadJob(label, overrideDisabled, forceUnload);
    jobs.erase(label);
    return 0;
}

json Manager::listJobs() {
    auto result = json::array();
    for (const auto & [label, job] : jobs) {
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

Manager::Manager(DomainType domain_) : domain(Domain(domain_)) {
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
    unloadAllJobs();
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

std::optional<Label> Manager::getLabelByPid(const pid_t pid) const {
    // TODO: switch to running_jobs
//    auto it = services.find(pid);
//    if (it != services.end()) {
//        return it->second;
//    } else {
//        return std::nullopt;
//    }
    for (const auto & [label, job] : jobs) {
        if (job.pid == pid) {
            return label;
        }
    }
    return std::nullopt;
}

bool Manager::jobExists(const Label &label) const {
    return jobs.count(label);
}

Job & Manager::getJob(const std::string &label) {
    return jobs.at(label);
}

void Manager::wakeJob(Job &job) {
    if (job.state != JOB_STATE_WAITING) {
        log_error("tried to wake job %s that was not asleep (state=%d)",
                  job.manifest.label.c_str(), job.state);
        throw std::logic_error("job in wrong state");
    }
    startJob(job);
}

void Manager::unloadAllJobs() {
    log_debug("unloading all jobs");
    auto it = jobs.begin();
    while (it != jobs.end()) {
        auto & job = it->second;
        try {
            unloadJob(job);
        } catch (...) {
            log_error("failed to unload %s: ignoring because all jobs are being unloaded",
                      job.manifest.label.c_str());
        }
        it = jobs.erase(it);
    }
}

void Manager::rescheduleCalendarJob(Job &job) {
    auto interval = job.manifest.calendar_interval.value();
    auto maybe_schedule = calendar::schedule_calendar_job(interval);
    if (!maybe_schedule) {
        return;
    }
    auto [absolute_time, relative_time] = maybe_schedule.value();
    log_debug("job %s scheduled to run in %d minutes at t=%ld", job.manifest.label.c_str(), relative_time,
              absolute_time);
    job.state = JOB_STATE_WAITING;
    auto &label = job.manifest.label;
    eventmgr.addTimer(relative_time, [label, this]() {
        if (jobExists(label)) {
            auto &job = getJob(label);
            // The job may have been unloaded in the interval, or manually started by an administrator.
            if (job.state == JOB_STATE_WAITING) {
                startJob(job);
                rescheduleCalendarJob(job);
            }
        }
    });
    // XXX-FIXME: update StateFile to set the absolute start time.
}

void Manager::reschedulePeriodicJob(Job &job) {
    log_debug("job %s will start after T=%u", job.manifest.label.c_str(), job.manifest.start_interval);
    const Label& label = job.manifest.label;
    eventmgr.addTimer(job.manifest.start_interval, [label, this]() {
        if (jobExists(label)) {
            auto &job = getJob(label);
            if (job.state == JOB_STATE_WAITING) {
                startJob(job);
                reschedulePeriodicJob(job);
            }
        }
    });
    job.state = JOB_STATE_WAITING;
}

void Manager::rescheduleStandardJob(Job &job) {
    const Label &label = job.manifest.label;
    time_t restart_at = job.started_at.value() + job.manifest.throttle_interval;
    time_t now = current_time();
    if (now >= restart_at) {
        log_debug("%s: restarting due to KeepAlive", job.manifest.label.c_str());
        startJob(job);
    } else {
        time_t delta = restart_at - now;
        if (delta > INT_MAX || delta < 0) {
            throw std::range_error("interval too large");
        }
        int seconds = static_cast<int>(delta);
        log_debug("%s: will restart in %d seconds due to KeepAlive setting", job.manifest.label.c_str(), seconds);
        job.state = JOB_STATE_WAITING;
        eventmgr.addTimer(seconds, [label, this]() {
            if (!jobExists(label)) {
                return;
            }
            auto &job = getJob(label);
            if (job.state == JOB_STATE_WAITING) {
                startJob(job);
            } else {
                log_debug("attempted to restart job %s: not waiting to be started", job.manifest.label.c_str());
            }
        });
    }
}

void Manager::rescheduleJob(Job &job) {
    assert(job.state == JOB_STATE_EXITED || job.state == JOB_STATE_LOADED);
    if (job.shouldStart()) {
        switch (job.schedule) {
            case JOB_SCHEDULE_PERIODIC:
                reschedulePeriodicJob(job);
                break;
            case JOB_SCHEDULE_CALENDAR:
                rescheduleCalendarJob(job);
                break;
            case JOB_SCHEDULE_NONE:
                rescheduleStandardJob(job);
                break;
            default:
                throw std::logic_error("wrong job type");
        }
    } else {
        log_debug("job is not supposed to be (re)started");
    }
}

void Manager::startJob(Job &job, std::optional<std::vector<Label>> visited) {
    log_debug("trying to start %s", job.manifest.label.c_str());

    // Check for a cyclic dependency that would cause infinite recursion.
    // Example: Job A depends on Job B, and Job B depends on Job A.
    if (visited) {
        log_debug("checking for cycle");
        for (const auto &label : visited.value()) {
            if (job.manifest.label == label) {
                log_error("cycle detected in the job dependency graph");
                return;
            }
        }
        // This is extra paranoia.
        // LCOV_EXCL_START
        if (visited.value().size() > 100) {
            log_error("dependency chain is too long; maximum number of jobs exceeded");
            return;
        }
        // LCOV_EXCL_STOP
    }

    // Start all dependencies
    for (const auto & [label, dep] : job.manifest.dependencies.getItemsByLabel()) {
        log_debug("evaluating dependency: %s", label.c_str());
        if (!jobExists(label)) {
            log_debug("dependency does not exist: %s", label.c_str());
            job.state = JOB_STATE_MISSING_DEPENDS;
            return;
        }
        auto & depjob = getJob(label);
        if (depjob.state == JOB_STATE_LOADED) {
            log_debug("starting job %s as a dependency of %s", depjob.manifest.label.c_str(), job.manifest.label.c_str());
            if (visited) {
                visited.value().emplace_back(job.manifest.label);
                startJob(depjob, visited);
            } else {
                std::vector<Label> dep_visited = {job.manifest.label};
                startJob(depjob, dep_visited);
            }
        }
        if (depjob.state != JOB_STATE_RUNNING) {
            log_debug("dependency is not running: %s", label.c_str());
            job.state = JOB_STATE_MISSING_DEPENDS;
            return;
        }
    }

    if (job.schedule != JOB_SCHEDULE_NONE) {
        // To "start" a scheduled job means scheduling it, not actually starting it.
        return rescheduleJob(job);
    }

    std::function<void()> post_fork_cleanup = [this]() {
        eventmgr.handleFork();
    };
    job.run(post_fork_cleanup);
    eventmgr.addProcess(job.pid, [this](pid_t pid, int status) {
        auto maybe_label = reapChildProcess(pid, status);
        if (maybe_label) {
            auto &job = getJob(maybe_label.value());
            rescheduleJob(job);
        }
    });
    job.state = JOB_STATE_RUNNING;
}

void Manager::startAllJobs() {
    for (auto & [label, job] : jobs) {
        if (job.shouldStart() && !job.hasStarted()) {
            startJob(job);
        }
    }
}

void Manager::loadDefaultManifests() {
    log_info("loading default manifests for domain %s", domain.to_string().c_str());
    auto paths = domain.getLoadPaths();
    for (const auto &path : paths) {
        (void) loadAllManifests(path);
    }
}

bool Manager::loadAllManifests(const std::string &path, bool overrideDisabled, bool forceLoad) {
    log_debug("loading all in %s", path.c_str());

    // FIXME: do this elsewhere
    // Replace ~ with the actual HOME
//    if (str.rfind("~", 0)) {
//        char *home = getenv("HOME");
//        if (!home) {
//            continue;
//        }
//            str.replace(0, 5, getenv("HOME"));
//        }
//    }

    bool error = false;
    if (!std::filesystem::exists(path)) {
        log_warning("load failed: path does not exist: %s", path.c_str());
        return false;
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

    return error;
}

