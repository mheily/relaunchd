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
#include <sstream>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "channel.h"
#include "clock.h"
#include "event.h"
#include "job.h"
#include "log.h"
#include "manager.h"
#include "options.h"
#include "rpc_server.h"
#include "signal_names.h"
#include "state_file.hpp"

using json = nlohmann::json;

static std::unique_ptr<StateFile> STATE_FILE;

std::optional<Label> Manager::reapChildProcess(pid_t pid, int status) {
    auto maybe_label = getLabelByPid(pid);
    if (!maybe_label) {
        // This can happen if we are a subreaper and a child exits after their
        // parent.
        log_debug("child pid %d exited but no matching job found", pid);
        return std::nullopt;
    }
    const auto &label = maybe_label.value();
    auto &job = getJob(label);
    // TODO: Set this:
    //      job.exit_status = status
    //  and get rid of:
    //      job.last_exit_status
    //      job.term_signal
    //  converting the above into methods that examine exit_status.
    if (WIFEXITED(status)) {
        job.last_exit_status = WEXITSTATUS(status);
        log_debug("job %s pid %d exited with status %d",
                  job.manifest.label.c_str(), job.pid, job.last_exit_status);
    } else if (WIFSIGNALED(status)) {
        job.last_exit_status = -1;
        job.term_signal = WTERMSIG(status);
        log_debug("job %s pid %d was terminated by signal %d",
                  job.manifest.label.c_str(), job.pid, job.term_signal);
    } else if (WIFSTOPPED(status)) {
        int stop_signal = WSTOPSIG(status);
        log_debug("job %s pid %d was stopped by signal %d",
                  job.manifest.label.c_str(), job.pid, stop_signal);
        return std::nullopt;
    } else {
        throw std::range_error("invalid status");
    }
    job.pid = 0;
    job.state = job_state::exited;
    job.killProcessGroup();
    return label;
}

void Manager::setupSignalHandlers() {
    // FIXME testing eventmgr.addSignal(SIGCHLD, [](int){});

    eventmgr.addSignal(SIGPIPE,
                       [](int) { log_debug("caught SIGPIPE and ignored it"); });

    eventmgr.addSignal(SIGINT, [this](int) {
        SHUTTING_DOWN = true;
        log_notice("caught SIGINT, exiting");
    });

    eventmgr.addSignal(SIGTERM, [this](int) {
        SHUTTING_DOWN = true;
        log_notice("caught SIGTERM, exiting");
    });
}

bool Manager::loadManifest(const std::filesystem::path &path,
                           bool overrideDisabled, bool forceLoad) {
    json obj = manifest::parse(path);
    return loadManifest(obj, path, overrideDisabled, forceLoad);
}

bool Manager::loadManifest(const json &jsondata, const std::string &path,
                           bool overrideDisabled, bool forceLoad) {
    Manifest manifest;
    try {
        manifest = jsondata.get<manifest::Manifest>();
    } catch (const std::exception &exc) {
        log_error("failed to parse manifest at %s: %s", path.c_str(),
                  exc.what());
        return false;
    }
    const auto &label = static_cast<std::string>(manifest.label);

    /* Check for duplicate jobs */
    if (jobExists(manifest.label)) {
        log_error("tried to load a duplicate job with label %s", label.c_str());
        return false;
    }

    if (overrideDisabled) {
        log_debug("%s: overriding the Disabled key", label.c_str());
        overrideJobEnabled(manifest.label, true);
    }

    // Check if the job is disabled
    if (manifest.disabled && !forceLoad) {
        auto state = STATE_FILE->getValue();
        if (state.at("Overrides").contains(label)) {
            const auto job_state = state["Overrides"][label];
            if (job_state.at("Enabled")) {
                forceLoad = true;
            }
        }
    }

    if (manifest.disabled && !forceLoad) {
        log_debug("will not load %s: it is disabled", label.c_str());
        return false;
    }

    auto [it, _] = jobs.emplace(label, Job{path, manifest});
    auto &job = it->second;
    job.load();

    return true;
}

bool Manager::unloadJob(std::unordered_map<std::string, Job>::iterator &it,
                        bool overrideDisabled, bool forceUnload) {
    Job &job = it->second;
    const auto &label = static_cast<std::string>(job.manifest.label);

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
        return false;
    }
    // FIXME: This should not be allowed. A job with a pid should not be
    // unloaded ever.
    if (job.pid) {
        job.killJob(SIGTERM);
        pid_t pid = job.pid;
        // TODO: this could scale poorly
        //  maybe add to a heap structure with an absolute time when the pid can
        //  be killed, then wakeup somewhere else?
        pending_sigkill.emplace_back(pid);
        eventmgr.addTimer(job.manifest.exit_timeout, [pid, this]() {
            ::kill(pid, SIGKILL);
            auto it =
                std::find(pending_sigkill.begin(), pending_sigkill.end(), pid);
            if (it != pending_sigkill.end()) {
                pending_sigkill.erase(it);
            }
        });
    }
    if (!job.unload()) {
        log_error("failed to unload %s", label.c_str());
        return false;
    }

    it = jobs.erase(it);

    return true;
}

bool Manager::unloadJob(Job &job, bool overrideDisabled, bool forceUnload) {
    auto it = jobs.find(static_cast<std::string>(job.manifest.label));
    if (it != jobs.end()) {
        return unloadJob(it, overrideDisabled, forceUnload);
    } else {
        log_info("tried to unload a job that is not loaded: %s",
                 job.manifest.label.c_str());
        return false;
    }
}

bool Manager::unloadJob(const Label &label, bool overrideDisabled,
                        bool forceUnload) {
    if (!jobExists(label)) {
        log_info("tried to unload a job that is not loaded: %s", label.c_str());
        return false;
    }
    auto &job = getJob(label);
    return unloadJob(job, overrideDisabled, forceUnload);
}

bool Manager::unloadJob(const std::filesystem::path &path,
                        bool overrideDisabled, bool forceUnload) {
    json obj;
    try {
        obj = manifest::parse(path);
    } catch (...) {
        log_error("error parsing %s", path.c_str());
        return false;
    }
    std::string buf;
    if (obj.contains("Label")) {
        obj.at("Label").get_to(buf);
        return unloadJob(Label{buf}, overrideDisabled, forceUnload);
    } else {
        log_error("manifest has no Label key");
        return false;
    }
}

json Manager::listJobs() {
    auto result = json::array();
    for (const auto &[label, job] : jobs) {
        std::string pid = (job.pid == 0) ? "-" : std::to_string(job.pid);
        if (job.pid == 0) {
            pid = "-";
        } else {
            pid = std::to_string(job.pid);
        }
        result.emplace_back(json::object({
            {"Label", std::string{job.manifest.label}},
            {"PID", std::move(pid)},
            {"LastExitStatus", job.last_exit_status},
        }));
    }
    return result;
}

void Manager::overrideJobEnabled(const Label &label_, bool enabled) {
    // FIXME: do we care if it exists?
    //  auto & job = manager_get_job_by_label(label);
    const auto &label = static_cast<std::string>(label_);
    auto doc = STATE_FILE->getValue();
    if (doc.at("Overrides").contains(label)) {
        doc["Overrides"][label]["Enabled"] = enabled;
    } else {
        doc["Overrides"].emplace(label, json::object({{"Enabled", enabled}}));
    }
    STATE_FILE->setValue(doc);
}

Manager::Manager(Domain domain_) : domain(std::move(domain_)) {
    const auto &statedir = domain.statedir;
    if (getuid() != 0 && !std::filesystem::exists(statedir)) {
        log_debug("creating %s", statedir.c_str());
        std::filesystem::create_directories(statedir);
    }

    json defaultStateDoc = {{"SchemaVersion", 1},
                            {"Overrides", json::object()}};
    auto statefilepath = domain.statedir / "state.json";
    STATE_FILE = std::make_unique<StateFile>(statefilepath, defaultStateDoc);

    setupSignalHandlers();
    // setup_socket_activation(main_kqfd);

    // Set up the RPC server
    auto sockfilename = domain.statedir / "rpc.sock";
    chan.bindAndListen(sockfilename, 1024);
    eventmgr.addSocketRead(chan.getSockFD(),
                           [this](int) { rpc_dispatch(chan, *this); });
}

Manager::~Manager() {
    log_debug("manager shutting down");
    unloadAllJobs();
    // Terminate all remaining child processes.
    // TODO: there should be a graceful shutdown() method that waits the
    // ExitInterval time
    for (auto pid : pending_sigkill) {
        (void)::kill(pid, SIGKILL);
        (void)::waitpid(pid, nullptr, 0);
    }
}

bool Manager::handleEvent(std::optional<std::chrono::milliseconds> timeout) {
    log_debug("waiting for an event");
    try {
        eventmgr.waitForEvent(timeout);
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
    for (const auto &[label, job] : jobs) {
        if (job.pid == pid) {
            return label;
        }
    }
    return std::nullopt;
}

bool Manager::jobExists(const Label &label) const {
    return jobs.count(static_cast<std::string>(label));
}

Job &Manager::getJob(const Label &label) {
    return jobs.at(static_cast<std::string>(label));
}

void Manager::wakeJob(Job &job) {
    if (job.state != job_state::waiting) {
        log_error("tried to wake job %s that was not asleep (state=%s)",
                  job.manifest.label.c_str(), job.getState());
        throw std::logic_error("job in wrong state");
    }
    startJob(job);
}

bool Manager::unloadAllJobs() noexcept {
    bool success = true;
    log_debug("unloading all jobs");
    auto it = jobs.begin();
    while (it != jobs.end()) {
        auto &job = it->second;
        bool result;
        try {
            result = unloadJob(it);
        } catch (...) {
            log_error("unhandled exception unloading %s", job.getLabel());
            result = false;
        }
        if (!result) {
            log_error("failed to unload %s: ignoring because all jobs are "
                      "being unloaded",
                      job.getLabel());
            success = false;
        }
    }
    return success;
}
//
// void Manager::rescheduleCalendarJob(Job &job) {
//    auto interval = job.manifest.calendar_interval.value();
//    auto maybe_schedule = calendar::schedule_calendar_job(interval);
//    if (!maybe_schedule) {
//        return;
//    }
//    auto [absolute_time, relative_time] = maybe_schedule.value();
//    log_debug("job %s scheduled to run in %lld minutes at t=%ld",
//              job.manifest.label.c_str(), (long long)relative_time.count(),
//              absolute_time);
//    job.state = job_state::waiting;
//    auto &label = job.manifest.label;
//    eventmgr.addTimer(relative_time, [label, this]() {
//        if (jobExists(label)) {
//            auto &job = getJob(label);
//            // The job may have been unloaded in the interval, or manually
//            // started by an administrator.
//            if (job.state == job_state::waiting) {
//                startJob(job);
//                rescheduleCalendarJob(job);
//            }
//        }
//    });
//    // XXX-FIXME: update StateFile to set the absolute start time.
//}

void Manager::reschedulePeriodicJob(Job &job) {
    log_debug("job %s will start after T=%u", job.manifest.label.c_str(),
              job.manifest.start_interval.value());
    const Label &label = job.manifest.label;
    std::chrono::milliseconds ms{job.manifest.start_interval.value()};
    eventmgr.addTimer(ms, [label, this]() {
        if (jobExists(label)) {
            auto &job = getJob(label);
            if (job.state == job_state::waiting) {
                startJob(job);
                reschedulePeriodicJob(job);
            }
        }
    });
    job.state = job_state::waiting;
}

void Manager::rescheduleStandardJob(Job &job) {
    const Label &label = job.manifest.label;
    if (!job.started_at) {
        log_debug("%s: starting for the first time",
                  job.manifest.label.c_str());
        return startJob(job);
    }

    time_t now = current_time();
    time_t restart_at = job.started_at.value() + job.manifest.throttle_interval;
    if (now >= restart_at) {
        log_debug("%s: restarting due to KeepAlive",
                  job.manifest.label.c_str());
        return startJob(job);
    }

    std::chrono::seconds seconds{restart_at - now};
    std::chrono::milliseconds milliseconds = seconds;
    log_debug("%s: will restart in %lld seconds due to KeepAlive setting",
              job.manifest.label.c_str(), (long long)seconds.count());
    job.state = job_state::waiting;
    eventmgr.addTimer(milliseconds, [label, this]() {
        if (!jobExists(label)) {
            return;
        }
        auto &job = getJob(label);
        if (job.state == job_state::waiting) {
            startJob(job);
        } else {
            log_debug("attempted to restart job %s: not waiting to be started",
                      job.manifest.label.c_str());
        }
    });
}

void Manager::rescheduleJob(Job &job) {
    assert(job.state == job_state::exited || job.state == job_state::loaded);
    if (job.shouldStart()) {
        switch (job.schedule) {
        case JOB_SCHEDULE_PERIODIC:
            reschedulePeriodicJob(job);
            break;
            //        case JOB_SCHEDULE_CALENDAR:
            //            rescheduleCalendarJob(job);
            //            break;
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

void Manager::startJob(Job &job) {
    log_debug("trying to start %s", job.manifest.label.c_str());

    if (job.schedule != JOB_SCHEDULE_NONE) {
        // To "start" a scheduled job means scheduling it, not actually starting
        // it.
        return rescheduleJob(job);
    }

    std::function<void()> post_fork_cleanup = [this]() {
        eventmgr.handleFork();
    };
    if (job.run(post_fork_cleanup)) {
        job.state = job_state::running;
        eventmgr.addProcess(job.pid, [this](pid_t pid, int status) {
            auto maybe_label = reapChildProcess(pid, status);
            if (maybe_label) {
                auto &job = getJob(maybe_label.value());
                rescheduleJob(job);
            }
        });
    } else {
        job.state = job_state::exited;
    }
}

void Manager::startAllJobs() {
    for (auto &[label, job] : jobs) {
        if (job.shouldStart() && !job.hasStarted()) {
            startJob(job);
        }
    }
}

void Manager::loadDefaultManifests() {
    log_info("loading default manifests for domain %s",
             domain.to_string().c_str());
    auto paths = domain.getLoadPaths();
    for (const auto &path : paths) {
        (void)loadAllManifests(path);
    }
}

bool Manager::loadAllManifests(const std::string &path, bool overrideDisabled,
                               bool forceLoad) {
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
        for (const auto &file : directory_iterator(path)) {
            try {
                if (!loadManifest(file.path(), overrideDisabled, forceLoad)) {
                    error = true;
                }
            } catch (...) {
                log_error("unhandled exception while parsing %s",
                          file.path().c_str());
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

bool Manager::killJob(const Label &label,
                      const std::string &signame_or_number) {
    auto maybe_signum = getSignalByName(signame_or_number);
    if (!maybe_signum) {
        return false;
    }
    if (!jobExists(label)) {
        log_debug("tried to kill a nonexistent job: %s", label.c_str());
        return false;
    }
    Job &job = getJob(label);
    bool success = job.killJob(maybe_signum.value());
    log_debug("sent signal %s to job %s with success=%d",
              signame_or_number.c_str(), label.c_str(), success);
    return success;
}
