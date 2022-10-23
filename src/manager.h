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

#pragma once

#include <filesystem>
#include "../vendor/json.hpp"

#include "channel.h"
#include "event.h"
#include "job.h"

class Manager {
public:
    Manager();

    virtual ~Manager();

    bool handleEvent();

    [[nodiscard]] std::optional<std::shared_ptr<Job>> at(pid_t pid) const;

    [[nodiscard]] std::optional<std::shared_ptr<Job>> at(const std::string &label) const;

    int erase(const std::string &label);

    void clear();

    bool loadAllManifests(const std::string &load_path, bool overrideDisabled = false, bool forceLoad = false);

    bool loadManifest(const std::filesystem::path &path, bool overrideDisabled = false, bool forceLoad = false);

    bool loadManifest(const json &jsondata, const std::string &path, bool overrideDisabled = false, bool forceLoad = false);

    void overrideJobEnabled(const std::string &label, bool enabled);

    json listJobs();

    int unloadJob(const std::string &label, bool overrideDisabled = false, bool forceUnload = false);

    int unloadJob(const std::filesystem::path &path, bool overrideDisabled = false, bool forceUnload = false);

    void startAllJobs();

private:

    void startJob(const std::shared_ptr<Job> &job);

    void wakeJob(std::shared_ptr<Job> &job);

    void rescheduleCalendarJob(const std::shared_ptr<Job> &job);

    void reschedulePeriodicJob(const std::shared_ptr<Job> &job);

    void rescheduleJob(const std::shared_ptr<Job> &job);

    void reapChildProcess(pid_t pid, int status);

    void setupSignalHandlers();

    std::unordered_map<std::string, std::shared_ptr<Job>> services;
    // TODO: switch from "services" to this:
    //std::unordered_map<std::string, std::shared_ptr<Job>> loaded_jobs;
    //std::unordered_map<pid_t, std::shared_ptr<Job>> running_jobs;

    kq::EventManager eventmgr;
    Channel chan;
    bool SHUTTING_DOWN = false;
};

/** Given a pending connection on a socket descriptor, activate the associated job */
//int manager_activate_job_by_fd(int fd);

/**
 * Given a process ID, find the associated job
 *
 * @return the job, or NULL if there are no matching jobs
 */
//std::shared_ptr<Job> manager_get_job_by_pid(pid_t pid);

/**
 * Given a label, find the associated job
 *
 * @return the job, or NULL if there are no matching jobs
 */
//std::shared_ptr<Job> manager_get_job_by_label(const std::string &label);

/**
 * Unload a job with a given <label>
 */
//int manager_unload_job(const char *label);

/**
 * Wake up a job that has been waiting for an external event.
 */
//int manager_wake_job(std::shared_ptr<Job> job);


//int manager_unload_manifest(const std::filesystem::path &path);
//int manager_unload_by_label(const std::string &label);
