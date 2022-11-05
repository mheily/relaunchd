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

#include "../vendor/json.hpp"
using json = nlohmann::json;

#include "manifest.h"
#include "log.h"

typedef std::string Label;

typedef enum {
	JOB_SCHEDULE_NONE = 0,
	JOB_SCHEDULE_PERIODIC,
	JOB_SCHEDULE_CALENDAR
} job_schedule_t;

enum job_state_e {
    JOB_STATE_DEFINED,
    JOB_STATE_LOADED,
    JOB_STATE_MISSING_DEPENDS,
    JOB_STATE_WAITING,
    JOB_STATE_RUNNING,
    JOB_STATE_KILLED,
    JOB_STATE_EXITED,
};

struct Job {
    Job(std::optional<std::filesystem::path> manifest_path_, Manifest manifest_);

    //! The time that the job started
    time_t started_at;

    std::optional<std::filesystem::path> manifest_path;
    const Manifest manifest;
    enum job_state_e state;
    pid_t pid;
    int last_exit_status, term_signal;
    job_schedule_t schedule;

    void dump() const {
        log_debug("job dump: label=%s state=%d", manifest.label.c_str(), state);
    }

    bool kill(int signum) const;

    bool kill(const std::string &signame_or_num);

    void run();

    void load();

    void unload();

    bool isRunning() const { return pid > 0; }

    //! Has the job ever been started by the manager? It might not be running now;
    //! to check if it is running, use isRunning() instead.
    bool hasStarted() const {
        switch (state) {
            case JOB_STATE_DEFINED:
            case JOB_STATE_LOADED:
            case JOB_STATE_MISSING_DEPENDS:
                return false;
            case JOB_STATE_WAITING:
            case JOB_STATE_RUNNING:
            case JOB_STATE_KILLED:
            case JOB_STATE_EXITED:
                return true;
            default:
                throw std::runtime_error("unhandled case");
        }
    }

    //! Should the job be started automatically?
    bool shouldStart() const {
        // FIXME: what about scheduled jobs?
        //  if    job->schedule != JOB_SCHEDULE_NONE
        if (state == JOB_STATE_LOADED &&
            (manifest.keep_alive.always || manifest.run_at_load)) {
            return true;
        } else {
            return false;
        }
    }

private:
    job_schedule_t _set_schedule() const;
};

