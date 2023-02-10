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

#include <csignal>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>

#ifdef __FreeBSD__
#include <sys/param.h>
#endif

#include "clock.h"
#include "config.h"
#include "exec_monitor.h"
#include "job.h"
#include "log.h"
#include "manager.h"

extern void keepalive_remove_job(const Job &job);

/* Add the standard set of environment variables that most programs expect.
 * See: http://pubs.opengroup.org/onlinepubs/009695399/basedefs/xbd_chap08.html
 * TODO: should cache these getenv() calls, so we don't do this dance for every
 * job invocation.
 */
static void
add_standard_environment_variables(std::vector<std::string> envvar) {
    static const char *keys[] = {"DISPLAY",
                                 /* Locale-related variables */
                                 "LC_ALL", "LC_COLLATE", "LC_CTYPE",
                                 "LC_MESSAGES", "LC_MONETARY", "LC_NUMERIC",
                                 "LC_TIME", "NLSPATH", "LANG",
                                 /* Misc */
                                 "TZ", NULL};
    const char **key = NULL, *envp = NULL;

    for (key = keys; *key != NULL; key++) {
        if ((envp = getenv(*key))) {
            auto keyval = std::string{*key} + "=" + std::string{envp};
            envvar.emplace_back(keyval);
        }
    }
}

std::vector<std::string>
Job::setup_environment_variables(const struct passwd *pwent) {
    std::vector<std::string> result;
    for (const auto &[key, val] : manifest.environment_variables) {
        std::string kv = std::string{key}.append("=").append(val);
        result.emplace_back(std::move(kv));
    }

    /* KLUDGE: when running as root, assume we are a system daemon and avoid
     * adding any session-related variables. This is why we need a proper Domain
     * variable for each job.
     *
     * The removal of these variables conforms to daemon(8) behavior on FreeBSD.
     */
    if (!pwent || pwent->pw_uid > 0) {
        std::string pw_name, pw_dir, pw_shell;
        if (pwent) {
            pw_name = pwent->pw_name;
            pw_dir = pwent->pw_dir;
            pw_shell = pwent->pw_shell;
        } else {
            // LCOV_EXCL_START
            pw_name = std::to_string(getuid());
            pw_dir = "/";
            pw_shell = "/bin/sh";
            // LCOV_EXCL_STOP
        }
        if (!manifest.environment_variables.count("LOGNAME")) {
            result.emplace_back(std::string{"LOGNAME="} + pw_name);
        }
        if (!manifest.environment_variables.count("USER")) {
            result.emplace_back(std::string{"USER="} + pw_name);
        }
        if (!manifest.environment_variables.count("HOME")) {
            result.emplace_back(std::string{"HOME="} + pw_dir);
        }
        if (!manifest.environment_variables.count("PATH")) {
            result.emplace_back(
                std::string{"PATH=/usr/bin:/bin:/usr/local/bin"});
        }
        if (!manifest.environment_variables.count("SHELL")) {
            result.emplace_back(std::string{"HOME="} + pw_shell);
        }
        if (!manifest.environment_variables.count("TMPDIR")) {
            result.emplace_back(std::string{"TMPDIR=/tmp"});
        }
        if (!manifest.environment_variables.count("PWD")) {
            // FIXME: should this be WorkingDirectory instead?
            result.emplace_back(std::string{"PWD=/"});
        }
    }

    add_standard_environment_variables(result);

#if 0
    // FIXME
    SLIST_FOREACH(jms, &job.manifest.sockets, entry) {
        job_manifest_socket_export(jms, env, offset++);
    }
    if (offset > 0) {
        if (asprintf(&buf, "LISTEN_FDS=%zu", offset) < 0) goto err_out;
        if (cvec_push(env, buf) < 0) goto err_out;
        free(buf);
        buf = NULL;

        if (asprintf(&buf, "LISTEN_PID=%d", getpid()) < 0) goto err_out;
        if (cvec_push(env, buf) < 0) goto err_out;
        free(buf);
        buf = NULL;
    }
#endif

    return result;
}

static std::optional<ExecStatus> replace_fd(int oldfd, const std::string &path,
                                            int flags, int mode) {
    int newfd = open(path.c_str(), flags, mode);
    if (newfd < 0) {
        return ExecStatus{ExecErrorCode::OpenFailed, errno};
    }
    if (dup2(newfd, oldfd) < 0) {
        (void)close(newfd);
        return ExecStatus{ExecErrorCode::Dup2Failed, errno};
    }
    if (close(newfd) < 0) {
        return ExecStatus{ExecErrorCode::CloseFailed, errno};
    }
    return std::nullopt;
}

std::optional<ExecStatus>
Job::start_child_process(const ExecutionContext &ctx) {
    if (manifest.umask) {
        (void)::umask(manifest.umask.value());
    } else {
        (void)::umask(S_IWGRP | S_IWOTH);
    }
    if (setsid() < 0) {
        return ExecStatus{ExecErrorCode::CreateSessionFailed, errno};
    }
    if (manifest.nice &&
        setpriority(PRIO_PROCESS, 0, manifest.nice.value()) < 0) {
        return ExecStatus{ExecErrorCode::SetPriorityFailed, errno};
    }
    if (manifest.working_directory &&
        chdir(manifest.working_directory->c_str()) < 0) {
        return ExecStatus{ExecErrorCode::SetWorkingDirectoryFailed, errno};
    }
    if (manifest.root_directory &&
        chroot(manifest.root_directory->c_str()) < 0) {
        return ExecStatus{ExecErrorCode::SetRootDirectoryFailed, errno};
    }
    if (manifest.user_name) {
        if (manifest.init_groups &&
            initgroups(manifest.user_name->c_str(), ctx.gid.value()) < 0) {
            return ExecStatus{ExecErrorCode::InitGroupsFailed, errno};
        }
        if (setgid(ctx.gid.value()) < 0) {
            return ExecStatus{ExecErrorCode::SetGroupIdFailed, errno};
        }
#if HAVE_SETLOGIN
        if (setlogin(manifest.user_name->c_str()) < 0) {
            return ExecStatus{ExecErrorCode::SetLoginFailed, errno};
        }
#endif
        if (setuid(ctx.uid.value()) < 0) {
            return ExecStatus{ExecErrorCode::SetUserIdFailed, errno};
        }
    }

    std::optional<ExecStatus> maybe_error;
    maybe_error = replace_fd(STDIN_FILENO, manifest.stdin_path, O_RDONLY, 0);
    if (maybe_error) {
        maybe_error->errorContext = ExecStatus::RedirectStdin;
        return maybe_error;
    }

    maybe_error = replace_fd(STDOUT_FILENO, manifest.stdout_path,
                             O_CREAT | O_WRONLY, 0600);
    if (maybe_error) {
        maybe_error->errorContext = ExecStatus::RedirectStdout;
        return maybe_error;
    }

    maybe_error = replace_fd(STDERR_FILENO, manifest.stderr_path,
                             O_CREAT | O_WRONLY, 0600);
    if (maybe_error) {
        maybe_error->errorContext = ExecStatus::RedirectStderr;
        return maybe_error;
    }

    char **envp =
        static_cast<char **>(calloc(ctx.environ.size() + 1, sizeof(char *)));
    if (!envp) {
        return ExecStatus{ExecErrorCode::MemoryAllocationFailed, errno};
    }
    for (size_t i = 0; i < ctx.environ.size(); i++) {
        envp[i] = const_cast<char *>(ctx.environ[i].c_str());
    }

    char **argv = static_cast<char **>(
        calloc(manifest.program_arguments.size() + 1, sizeof(char *)));
    if (!argv) {
        free(envp);
        return ExecStatus{ExecErrorCode::MemoryAllocationFailed, errno};
    }
    for (size_t i = 0; i < manifest.program_arguments.size(); i++) {
        argv[i] = const_cast<char *>(manifest.program_arguments[i].c_str());
    }

    char *path;
    if (manifest.program) {
        path = (char *)manifest.program.value().c_str();
    } else {
        path = argv[0];
    }
    // TODO: move this to manifest load-time error
    if (!path) {
        throw std::logic_error("path cannot be empty");
    }
//    if (job.manifest.enable_globbing) {
//        // TODO: globbing
//    }
#if DEBUG_EXEC_CALL
    log_debug("exec: %s", path);

    log_debug("argv[]:");
    for (char **item = argv; *item; item++) {
        log_debug(" - arg: %s", *item);
    }
    log_debug("envp[]:");
    for (char **item = envp; *item; item++) {
        log_debug(" - env: %s", *item);
    }
#endif

    // TODO: reenable this if openlog() doesn't set FD_CLOEXEC
    // closelog();

    (void)execve(path, argv, envp);
    int saved_errno = errno;
    free(argv);
    free(envp);
    return ExecStatus{ExecErrorCode::ExecFailed, saved_errno};
}

bool Job::run(const std::function<void()> post_fork_cleanup) {
    std::optional<uid_t> uid;
    std::optional<uid_t> gid;

    struct group *grent;
    if (manifest.group_name) {
        grent = ::getgrnam(manifest.group_name.value().c_str());
    } else {
        grent = ::getgrgid(getgid());
    }

    struct passwd *pwent;
    if (manifest.user_name) {
        pwent = ::getpwnam(manifest.user_name.value().c_str());
    } else {
        pwent = ::getpwuid(getuid());
    }

    if (manifest.user_name) {
        uid = pwent->pw_uid;
        if (manifest.group_name) {
            gid = grent->gr_gid;
        } else {
            gid = pwent->pw_gid;
        }
    }

    ExecutionContext ctx{uid, gid, setup_environment_variables(pwent)};

    ExecMonitor ipcpipe;
    ipcpipe.createPipe();

    pid = fork();
    if (pid < 0) {
        log_errno("fork(2)");
        return false;
    } else if (pid == 0) {
        // This is the child process.
        ipcpipe.becomeChild();
        try {
            post_fork_cleanup();
        } catch (...) {
            log_error("post_fork_cleanup() failed");
            ipcpipe.writeStatus(ExecStatus{ExecErrorCode::ForkHandlerFailed});
        }
        auto maybe_error = start_child_process(ctx);
        if (maybe_error) {
            ipcpipe.writeStatus(*maybe_error);
        }
        exit(127);
    } else {
        // This is the parent process.
        started_at = current_time();
        log_debug("job %s started at %lld with pid %d", manifest.label.c_str(),
                  static_cast<long long>(started_at.value()), pid);

        ipcpipe.becomeParent();
        ExecStatus status = ipcpipe.readStatus();
        if (status.errorCode == ExecErrorCode::ExecSuccess) {
            pgid = getpgid(pid);
            if (pgid < 0) {
                log_errno("getpgid(pid=%d) failed", pid);
            }
            return true;
        } else {
            log_error("job %s failed to start: %s", manifest.label.c_str(),
                      status.toString().c_str());
            ::kill(pid, 9);
            ::waitpid(pid, nullptr, 0);
            pid = 0;
            return false;
        }
    }
}

job_schedule_t Job::_set_schedule() const {
    if (manifest.start_interval > 0) {
        return JOB_SCHEDULE_PERIODIC;
        //    } else if (manifest.calendar_interval) {
        //        return JOB_SCHEDULE_CALENDAR;
    } else {
        return JOB_SCHEDULE_NONE;
    }
}

Job::Job(std::optional<std::filesystem::path> manifest_path_,
         Manifest manifest_, kq::EventManager &eventmgr_,
         StateFile &state_file_)
    : manifest_path(std::move(manifest_path_)), manifest(std::move(manifest_)),
      pid(0), pgid(-1), last_exit_status(0), term_signal(0),
      schedule(_set_schedule()), eventmgr(eventmgr_), state_file(state_file_) {
    initFSM();
}

void Job::initFSM() {
    fsm.add_transitions(
        {// From: Loaded
         // To: Any state
         {
             States::Loaded,
             States::Running,
             Triggers::Bootstrap,
             [this] {
                 return !isDisabled() &&
                        (manifest.run_at_load || manifest.keep_alive.always);
             },
             [this] { fsm.execute(Triggers::StartRequested); },
         },
         {
             States::Loaded,
             States::Waiting,
             Triggers::Bootstrap,
             [this] {
                 return !isDisabled() && manifest.start_interval.has_value() &&
                        !manifest.run_at_load;
             },
             [this] { schedulePeriodicJob(); },
         },
         {
             States::Loaded,
             States::Running,
             Triggers::StartRequested,
             [] { return true; },
             [this] { startJob(); },
         },
         {
             States::Loaded,
             States::Unloaded,
             Triggers::UnloadRequested,
             [] { return true; },
             [this] {
                 eventmgr.submitIpcCallback("delete_job", manifest.label.str());
             },
         },
         // From: Waiting
         // To: Any state
         {
             States::Waiting,
             States::Running,
             Triggers::StartRequested,
             [] { return true; },
             [this] {
                 if (timer_id) {
                     cancelTimer();
                 } // why wouldn't it have a timer if it is waiting??
                 startJob();
             },
         },
         {
             States::Waiting,
             States::Unloaded,
             Triggers::UnloadRequested,
             [] { return true; },
             [this] {
                 if (timer_id) {
                     cancelTimer();
                 }
                 eventmgr.submitIpcCallback("delete_job", manifest.label.str());
             },
         },
         // From: Running
         // To: Any state
         {States::Running, States::Exited, Triggers::ProcessExited,
          [this] {
              return !manifest.keep_alive.always &&
                     !manifest.start_interval.has_value() && !unload_requested;
          },
          [this] {
              log_notice("job %s: transitioned to Exited state", getLabel());
          }},
         {
             States::Running,
             States::Running,
             Triggers::ProcessExited,
             [this] {
                 return manifest.keep_alive.always && !shouldThrottle() &&
                        !unload_requested;
             },
             [this] { startJob(); },
         },
         {
             States::Running,
             States::Running,
             Triggers::StopRequested,
             [] { return true; },
             [this] {
                 // TODO: handle errors?
                 killJob(SIGTERM);
             },
         },
         {
             States::Running,
             States::Waiting,
             Triggers::ProcessExited,
             [this] {
                 return manifest.keep_alive.always && shouldThrottle() &&
                        !unload_requested;
             },
             [this] { startAfterThrottleInterval(); },
         },
         {
             States::Running,
             States::Unloaded,
             Triggers::ProcessExited,
             [this] { return unload_requested; },
             [this] {
                 eventmgr.submitIpcCallback("delete_job", manifest.label.str());
             },
         },
         // From: Exited
         // To: Any state
         {
             States::Exited,
             States::Unloaded,
             Triggers::UnloadRequested,
             [] { return true; },
             [this] {
                 eventmgr.submitIpcCallback("delete_job", manifest.label.str());
             },
         }});
    fsm.add_debug_fn([this](Job::States from_state, Job::States to_state,
                            Job::Triggers trigger) {
        log_debug(
            "job %s: trigger %s caused the state to change from %s to %s ",
            getLabel(), triggerToString(trigger), stateToString(from_state),
            stateToString(to_state));
    });
}

bool Job::killJob(int signum) const noexcept {
    if (pid == 0) {
        log_warning("tried to send a signal to a job that is not running");
        return true;
    }
    if (::kill(pid, signum) < 0) {
        if (errno == ESRCH) {
            log_debug("kill(2) of pid %d: got ESRCH", pid);
        } else {
            log_errno("kill(2) of pid %d", pid);
            return false;
        }
        log_error("kill(2) of PID %d failed: %s", pid, strerror(errno));
        return false;
    }
    log_notice("sent signal %d to process %d for job %s", signum, pid,
               manifest.label.c_str());
    return true;
}

bool Job::killProcessGroup() const noexcept {
    if (pgid < 0) {
        log_warning("job %s has no process group ID", manifest.label.c_str());
        return false;
    }
    if (manifest.abandon_process_group) {
        log_info("process group %d will be abandoned", pgid);
        return false;
    }
    pid_t negative_pgid = -1 * pgid;
    for (;;) {
        log_debug("sending SIGKILL to process group %d", pgid);
        if (killpg(pgid, SIGKILL) == -1) {
            if (errno != ESRCH && errno != EPERM) {
                log_errno("killpg(pgid=%d)", pgid);
                return false;
            }
        }
        int rv = waitpid(negative_pgid, nullptr, 0);
        if (rv < 0) {
            if (errno == ECHILD) {
                log_debug(
                    "all child processes in process group %d have been reaped",
                    pgid);
            } else {
                log_errno("waitpid(2)");
                return false;
            }
        }
        if (rv <= 0) {
            break;
        }
    }
    return true;
}

const char *Job::stateToString(const Job::States &state) {
    switch (state) {
    case States::Loaded:
        return "loaded";
    case States::Waiting:
        return "waiting";
    case States::Running:
        return "running";
    case States::Exited:
        return "exited";
    case States::Unloaded:
        return "unloaded";
    default:
        __builtin_unreachable();
    }
}

const char *Job::triggerToString(const Job::Triggers &trigger) {
    switch (trigger) {
    case Job::Triggers::Bootstrap:
        return "Bootstrap";
    case Job::Triggers::StartRequested:
        return "StartRequested";
    case Job::Triggers::StopRequested:
        return "StopRequested";
    case Job::Triggers::ProcessExited:
        return "ProcessExited";
    case Job::Triggers::UnloadRequested:
        return "UnloadRequested";
    default:
        __builtin_unreachable();
    }
}

const char *Job::getState() const { return stateToString(fsm.state()); }

void Job::reapChildProcess(int status) {
    // TODO: Set this:
    //      job.exit_status = status
    //  and get rid of:
    //      job.last_exit_status
    //      job.term_signal
    //  converting the above into methods that examine exit_status.
    if (WIFEXITED(status)) {
        last_exit_status = WEXITSTATUS(status);
        log_debug("job %s pid %d exited with status %d", manifest.label.c_str(),
                  pid, last_exit_status);
    } else if (WIFSIGNALED(status)) {
        last_exit_status = -1;
        term_signal = WTERMSIG(status);
        log_debug("job %s pid %d was terminated by signal %d",
                  manifest.label.c_str(), pid, term_signal);
    } else if (WIFSTOPPED(status)) {
        throw std::logic_error(
            "This method should not be called when the process is stopped");
    } else {
        throw std::range_error("invalid status");
    }
    pid = 0;
    killProcessGroup();
}

void Job::startJob() {
    log_notice("starting job: %s", getLabel());
    started_at = current_time();
    std::function<void()> const post_fork_cleanup = [this]() {
        eventmgr.handleFork();
    };
    if (run(post_fork_cleanup)) {
        eventmgr.addProcess(pid, [this](pid_t, int status) {
            if (WIFSTOPPED(status)) {
                int stop_signal = WSTOPSIG(status);
                log_info("job %s: pid %d was stopped by signal %d", getLabel(),
                         pid, stop_signal);
            } else {
                reapChildProcess(status);
                fsm.execute(Job::Triggers::ProcessExited);
            }
        });
    } else {
        log_error("FIXME -- what to do here?? throttle and retry?");
    }
}

void Job::startAfterThrottleInterval() {
    time_t elapsed = current_time() - *started_at;
    const std::chrono::seconds seconds{manifest.throttle_interval - elapsed};
    const std::chrono::milliseconds milliseconds = seconds;
    log_debug("%s: will restart in %lld seconds due to KeepAlive setting",
              manifest.label.c_str(), (long long)seconds.count());
    timer_id = eventmgr.addTimer(milliseconds, [this]() {
        timer_id = std::nullopt;
        fsm.execute(Triggers::StartRequested);
    });
}

void Job::schedulePeriodicJob() {
    assert(!timer_id);
    log_debug("periodic job %s will start after T=%u", getLabel(),
              manifest.start_interval.value());
    std::chrono::milliseconds const ms{manifest.start_interval.value()};
    timer_id = eventmgr.addTimer(ms, [this]() {
        timer_id = std::nullopt;
        startJob();
    });
}

void Job::forceUnloadJob() noexcept {
    if (pid) {
        log_debug("%s: sending SIGKILL to pid %d", getLabel(), pid);
        kill(pid, SIGKILL);
        if (waitpid(pid, nullptr, 0) < 0) {
            log_errno("waitpid(2)");
        }
        killProcessGroup();
        eventmgr.deleteProcess(pid);
        pid = 0;
        pgid = -1;
    }
    if (timer_id) {
        cancelTimer();
    }
    fsm.reset(Job::States::Unloaded);
}

bool Job::shouldThrottle() {
    time_t elapsed = current_time() - *started_at;
    return elapsed < manifest.throttle_interval;
}

bool Job::isDisabled() const {
    const std::string &label = manifest.label.str();
    const auto state = state_file.getValue();
    if (state.at("Overrides").contains(label)) {
        const auto job_state = state["Overrides"][label];
        return !job_state.at("Enabled");
    } else {
        return manifest.disabled;
    }
}

bool Job::unloadJob(bool forceUnload) {
    if (fsm.state() == Job::States::Unloaded) {
        log_debug("tried to unload a job that is already unloaded: %s",
                  getLabel());
        return false;
    }
    if (unload_requested) {
        log_debug("tried to unload a job that is already in the process of "
                  "unloading: %s",
                  getLabel());
        return false;
    } else {
        unload_requested = true;
    }
    if (isDisabled() && !forceUnload) {
        log_debug("will not unload %s: it is disabled", getLabel());
        return false;
    }
    log_notice("unloading job %s", getLabel());
    fsm.execute(Job::Triggers::StopRequested);
    fsm.execute(Job::Triggers::UnloadRequested);
    return true;
}

void Job::cancelTimer() {
    log_debug("cancelling timer ID %d", *timer_id);
    eventmgr.deleteTimer(timer_id.value());
    timer_id = std::nullopt;
}
