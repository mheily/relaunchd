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

#include <grp.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

#include "event.h"
#include "exec_monitor.h"
#include "fsm.h"
#include "log.h"
#include "manifest.h"
#include "state_file.hpp"

struct ExecutionContext {
    std::optional<gid_t> gid;
    std::optional<uid_t> uid;
    std::vector<std::string> environ;
};

typedef enum {
    JOB_SCHEDULE_NONE = 0,
    JOB_SCHEDULE_PERIODIC,
    JOB_SCHEDULE_CALENDAR
} job_schedule_t;

struct Job {
    friend class Manager;
    friend struct ManagerTest;

    Job(std::optional<std::filesystem::path> manifest_path_, Manifest manifest_,
        kq::EventManager &eventmgr, StateFile &state_file_);

  protected:
    //! The time that the job started
    std::optional<time_t> started_at;

    std::optional<std::filesystem::path> manifest_path;
    const Manifest manifest;
    pid_t pid, pgid;
    int last_exit_status, term_signal;
    job_schedule_t schedule;

    const char *getLabel() const { return manifest.label.c_str(); }

    //! Get the current state
    const char *getState() const;

    void dump() const {
        log_debug("job dump: label=%s state=%s", manifest.label.c_str(),
                  getState());
    }

    bool killJob(int signum) const noexcept;

    bool killProcessGroup() const noexcept;

    bool run(std::function<void()> post_fork_cleanup);

    //! Return true if the job is disabled
    [[nodiscard]] bool isDisabled() const;

  private:
    std::vector<std::string>
    setup_environment_variables(const struct passwd *pwent);
    std::optional<ExecStatus> start_child_process(const ExecutionContext &ctx);
    job_schedule_t _set_schedule() const;
    void reapChildProcess(int status);
    // Used by job_state::starting
    // ExecMonitor ipcpipe;
    // std::optional<ExecStatus> exec_status;

    // FSM implementation
    enum class States { Loaded, Waiting, Running, Exited, Unloaded };
    enum class Triggers {
        Bootstrap,
        StartRequested,
        StopRequested,
        ProcessExited,
        UnloadRequested
    };
    FSM::Fsm<States, States::Loaded, Triggers> fsm;
    static const char *stateToString(const States &state);
    static const char *triggerToString(const Triggers &trigger);

    std::optional<int> timer_id;

    //! If true, the job is in the process of being unloaded
    bool unload_requested = false;

    // Shared with the ::Manager of this job
    kq::EventManager &eventmgr;
    const StateFile &state_file;

    void initFSM();
    void startJob();

    //! Send a SIGTERM and wait for the process to exit gracefully.
    bool unloadJob(bool forceUnload);

    //! Send a SIGKILL to the process and transition to the Unloaded state.
    void forceUnloadJob() noexcept;

    // TODO? killJob(SIGTERM) is used now.
    //  void stopJob();
    void startAfterThrottleInterval();
    void schedulePeriodicJob();
    bool shouldThrottle();
    void cancelTimer();
};
