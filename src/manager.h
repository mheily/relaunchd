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
#include "state_file.hpp"

//! When the manager finishes, it returns a result that is used to
//! reboot/poweroff/etc.
enum class ManagerResult {
    //! The default action is to call exit() after the manager returns. This
    //! only works when running as non-PID 1.
    Exit,

    //! Sending SIGINT will reboot the system.
    Reboot,

    //! Sending SIGUSR1 will halt the system.
    Halt,

    //! Sending SIGUSR2 will halt and power down the system.
    HaltAndPowerDown,

    //! Sending SIGTERM will transition to single-user mode
    SingleUserMode,

    // TODO: handle other signals related to gettys
};

class Manager {
    friend struct ManagerTest;

  public:
    Manager(Domain domain_);

    Manager() : Manager(Domain{}) {}

    virtual ~Manager();

    bool handleEvent(
        std::optional<std::chrono::milliseconds> timeout = std::nullopt);

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

    bool unloadJob(const Label &label, bool overrideDisabled = false,
                   bool forceUnload = false);

    bool unloadJob(const std::filesystem::path &path,
                   bool overrideDisabled = false, bool forceUnload = false);

    bool unloadJob(Job &job, bool overrideDisabled = false,
                   bool forceUnload = false);

    bool unloadAllJobs() noexcept;

    bool killJob(const Label &, const std::string &signame_or_number);

    //! Return true if the job exists
    bool jobExists(const Label &label) const;

    //! Dump job information to the log
    void dumpJob(const Label &label) const;

    //! Delete all custom state settings
    void clearStateFile();

    const Domain &getDomain() const;

    void startRunning();

    void stopRunning();

    //! Run the event processing loop until shutdown is complete
    ManagerResult runMainLoop();

    //! Run a single iteration of the event processing loop, with an optional
    //! timeout
    bool
    runOnce(std::optional<std::chrono::milliseconds> timeout = std::nullopt);

  private:
    void initFSM();

    void startRpcServer();

    void startAllJobs();

    static StateFile createOrOpenStatefile(const Domain &);

    void forceUnloadAllJobs() noexcept;

    Job &getJob(const Label &label) const;

    void startJob(Job &job);

    void setupSignalHandlers();

    void handleShutdownSignal(const std::string &signame);

    //! Jobs that have been queued for loading but are waiting for a
    //! StartAllJobs() signal
    std::unordered_map<std::string, std::unique_ptr<Job>> pending_jobs;

    std::unordered_map<std::string, std::unique_ptr<Job>> jobs;
    const Domain domain;
    kq::EventManager eventmgr;
    Channel chan;
    StateFile state_file;

    // FSM implementation
    enum class States { Unconfigured, Running, GracefulShutdown, Finished };
    enum class Triggers { StartRequested, StopRequested, AllJobsExited };
    FSM::Fsm<States, States::Unconfigured, Triggers> fsm;
    static const char *stateToString(const States &state);
    static const char *triggerToString(const Triggers &trigger);

    //! Temporary holding area for the return value of runMainLoop().
    ManagerResult pending_result = ManagerResult::Exit;

    // FIXME: actually implement this
    //! Once the GracefulShutdown process starts, this is the deadline for it to
    //! finish.
    // If the deadline is exceeded, all jobs will be forcefully killed.
    std::optional<time_t> shutdownDeadline;
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
