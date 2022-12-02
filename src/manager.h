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

#include <array>
#include <filesystem>
#include <nlohmann/json.hpp>

#include "channel.h"
#include "domain.h"
#include "event.h"
#include "job.h"

class Manager {
  public:
    Manager(DomainType domain_);

    virtual ~Manager();

    bool handleEvent(
        std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    [[nodiscard]] std::optional<Label> getLabelByPid(pid_t pid) const;

    Job &getJob(const Label &label);

    void unloadAllJobs();

    void loadDefaultManifests();

    bool loadAllManifests(const std::string &load_path,
                          bool overrideDisabled = false,
                          bool forceLoad = false);

    bool loadManifest(const std::filesystem::path &path,
                      bool overrideDisabled = false, bool forceLoad = false);

    bool loadManifest(const json &jsondata, const std::string &path,
                      bool overrideDisabled = false, bool forceLoad = false);

    void overrideJobEnabled(const Label &label, bool enabled);

    json listJobs();

    int unloadJob(const std::string &label, bool overrideDisabled = false,
                  bool forceUnload = false);

    int unloadJob(const std::filesystem::path &path,
                  bool overrideDisabled = false, bool forceUnload = false);

    void unloadJob(Job &job, bool overrideDisabled = false,
                   bool forceUnload = false);

    bool killJob(const Label &, const std::string &signame_or_number);

    void startAllJobs();

    //! Return true if the job exists
    bool jobExists(const Label &label) const;

  private:
    void startJob(Job &job,
                  std::optional<std::vector<Label>> visited = std::nullopt);

    void wakeJob(Job &job);

    void rescheduleCalendarJob(Job &job);

    void reschedulePeriodicJob(Job &job);

    void rescheduleJob(Job &job);

    std::optional<Label> reapChildProcess(pid_t pid, int status);

    void setupSignalHandlers();

    std::unordered_map<std::string, Job> jobs;

    Domain domain;
    kq::EventManager eventmgr;
    Channel chan;
    bool SHUTTING_DOWN = false;

    void rescheduleStandardJob(Job &job);
};

/** Given a pending connection on a socket descriptor, activate the associated
 * job */
// int manager_activate_job_by_fd(int fd);

/**
 * Given a process ID, find the associated job
 *
 * @return the job, or NULL if there are no matching jobs
 */
// std::shared_ptr<Job> manager_get_job_by_pid(pid_t pid);

/**
 * Given a label, find the associated job
 *
 * @return the job, or NULL if there are no matching jobs
 */
// std::shared_ptr<Job> manager_get_job_by_label(const std::string &label);

/**
 * Unload a job with a given <label>
 */
// int manager_unload_job(const char *label);

/**
 * Wake up a job that has been waiting for an external event.
 */
// int manager_wake_job(std::shared_ptr<Job> job);

// int manager_unload_manifest(const std::filesystem::path &path);
// int manager_unload_by_label(const std::string &label);
