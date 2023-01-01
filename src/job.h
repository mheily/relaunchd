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

#include "log.h"
#include "manifest.h"

typedef enum {
    JOB_SCHEDULE_NONE = 0,
    JOB_SCHEDULE_PERIODIC,
    JOB_SCHEDULE_CALENDAR
} job_schedule_t;

enum class job_state {
    defined,
    loaded,
    missing_depends,
    waiting,
    running,
    exited,
};

struct Job {
    Job(std::optional<std::filesystem::path> manifest_path_,
        Manifest manifest_);

    //! The time that the job started
    std::optional<time_t> started_at;

    std::optional<std::filesystem::path> manifest_path;
    const Manifest manifest;
    job_state state;
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

    void load();

    bool unload();

    //! Has the job ever been started by the manager? It might not currently
    // be running, but that is okay.
    // TODO: Replace this with a state machine that cannot transition back to a
    //       non-started state.
    bool hasStarted() const {
        switch (state) {
        case job_state::defined:
        case job_state::loaded:
        case job_state::missing_depends:
            return false;
        case job_state::waiting:
        case job_state::running:
        case job_state::exited:
            return true;
        default:
            __builtin_unreachable();
        }
    }

    //! Should the job be started automatically?
    bool shouldStart() const {
        switch (state) {
        case job_state::defined:
            return false;
        case job_state::missing_depends: // not sure about this one...
            return false;
        case job_state::loaded:
            return (manifest.run_at_load || manifest.keep_alive.always ||
                    schedule != JOB_SCHEDULE_NONE);
        case job_state::waiting:
            // If true, a non-scheduled job was scheduled due to
            // ThrottleInterval
            return schedule != JOB_SCHEDULE_NONE;
        case job_state::running:
            return false;
        case job_state::exited:
            return (manifest.keep_alive.always ||
                    schedule != JOB_SCHEDULE_NONE);
        default:
            __builtin_unreachable();
        }
    }

  private:
    job_schedule_t _set_schedule() const;
};
