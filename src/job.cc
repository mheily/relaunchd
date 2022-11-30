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

#include "calendar.h"
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

static std::vector<std::string>
setup_environment_variables(const Job &job, const struct passwd *pwent) {
    std::vector<std::string> result;
    for (const auto &[key, val] : job.manifest.environment_variables) {
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
        if (!job.manifest.environment_variables.count("LOGNAME")) {
            result.emplace_back(std::string{"LOGNAME="} + pw_name);
        }
        if (!job.manifest.environment_variables.count("USER")) {
            result.emplace_back(std::string{"USER="} + pw_name);
        }
        if (!job.manifest.environment_variables.count("HOME")) {
            result.emplace_back(std::string{"HOME="} + pw_dir);
        }
        if (!job.manifest.environment_variables.count("PATH")) {
            result.emplace_back(
                std::string{"PATH=/usr/bin:/bin:/usr/local/bin"});
        }
        if (!job.manifest.environment_variables.count("SHELL")) {
            result.emplace_back(std::string{"HOME="} + pw_shell);
        }
        if (!job.manifest.environment_variables.count("TMPDIR")) {
            result.emplace_back(std::string{"TMPDIR=/tmp"});
        }
        if (!job.manifest.environment_variables.count("PWD")) {
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
        return ExecStatus{ExecStatus::OpenFailed, errno};
    }
    if (dup2(newfd, oldfd) < 0) {
        (void)close(newfd);
        return ExecStatus{ExecStatus::Dup2Failed, errno};
    }
    if (close(newfd) < 0) {
        return ExecStatus{ExecStatus::CloseFailed, errno};
    }
    return std::nullopt;
}

static std::optional<ExecStatus>
start_child_process(const Job &job, const std::vector<std::string> &final_env,
                    const struct passwd *pwent, const struct group *grent) {
    const Manifest &manifest = job.manifest;

    if (setsid() < 0) {
        return ExecStatus{ExecStatus::CreateSessionFailed, errno};
    }
    if (manifest.nice &&
        setpriority(PRIO_PROCESS, 0, manifest.nice.value()) < 0) {
        return ExecStatus{ExecStatus::SetPriorityFailed, errno};
    }
    if (manifest.working_directory &&
        chdir(manifest.working_directory->c_str()) < 0) {
        return ExecStatus{ExecStatus::SetWorkingDirectoryFailed, errno};
    }
    if (manifest.root_directory &&
        chroot(manifest.root_directory->c_str()) < 0) {
        return ExecStatus{ExecStatus::SetRootDirectoryFailed, errno};
    }
    if (manifest.user_name) {
        gid_t new_gid;
        if (manifest.group_name) {
            new_gid = grent->gr_gid;
        } else {
            new_gid = pwent->pw_gid;
        }
        if (manifest.init_groups &&
            initgroups(manifest.user_name->c_str(), new_gid) < 0) {
            return ExecStatus{ExecStatus::InitGroupsFailed, errno};
        }
        if (setgid(new_gid) < 0) {
            return ExecStatus{ExecStatus::SetGroupIdFailed, errno};
        }
#if HAVE_SETLOGIN
        if (setlogin(manifest.user_name->c_str()) < 0) {
            return ExecStatus{ExecStatus::SetLoginFailed, errno};
        }
#endif
        if (setuid(pwent->pw_uid) < 0) {
            return ExecStatus{ExecStatus::SetUserIdFailed, errno};
        }
    }
    // FIXME: convert umask to octal
    //(void) umask(job.manifest.umask);

    std::optional<ExecStatus> maybe_error;
    maybe_error =
        replace_fd(STDIN_FILENO, job.manifest.stdin_path, O_RDONLY, 0);
    if (maybe_error) {
        maybe_error->errorContext = ExecStatus::RedirectStdin;
        return maybe_error;
    }

    maybe_error = replace_fd(STDOUT_FILENO, job.manifest.stdout_path,
                             O_CREAT | O_WRONLY, 0600);
    if (maybe_error) {
        maybe_error->errorContext = ExecStatus::RedirectStdout;
        return maybe_error;
    }

    maybe_error = replace_fd(STDERR_FILENO, job.manifest.stderr_path,
                             O_CREAT | O_WRONLY, 0600);
    if (maybe_error) {
        maybe_error->errorContext = ExecStatus::RedirectStderr;
        return maybe_error;
    }

    char **envp =
        static_cast<char **>(calloc(final_env.size() + 1, sizeof(char *)));
    if (!envp) {
        return ExecStatus{ExecStatus::MemoryAllocationFailed, errno};
    }
    for (size_t i = 0; i < final_env.size(); i++) {
        envp[i] = const_cast<char *>(final_env[i].c_str());
    }

    char **argv = static_cast<char **>(
        calloc(job.manifest.program_arguments.size() + 1, sizeof(char *)));
    if (!argv) {
        return ExecStatus{ExecStatus::MemoryAllocationFailed, errno};
    }
    for (size_t i = 0; i < job.manifest.program_arguments.size(); i++) {
        argv[i] = const_cast<char *>(job.manifest.program_arguments[i].c_str());
    }

    char *path;
    if (job.manifest.program) {
        path = (char *)job.manifest.program.value().c_str();
    } else {
        path = argv[0];
    }
    // TODO: move this to manifest load-time error
    if (!path) {
        throw std::logic_error("path cannot be empty");
    }
    if (job.manifest.enable_globbing) {
        // TODO: globbing
    }
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
    return ExecStatus{ExecStatus::ExecFailed, saved_errno};
}

void Job::load() {
    /* TODO: This is the place to setup on-demand watches for the following
       keys: WatchPaths QueueDirectories
    */
// FIXME: sockets
#if 0
    struct job_manifest_socket *jms;

	if (!SLIST_EMPTY(&job.manifest.sockets)) {
		SLIST_FOREACH(jms, &job.manifest.sockets, entry) {
			if (job_manifest_socket_open(job, jms) < 0) {
				log_error("failed to open socket");
				return (-1);
			}
		}
		log_debug("job %s sockets created", job.manifest.label.c_str());
		job->state = JOB_STATE_WAITING;
		return (0);
	}
#endif

    state = JOB_STATE_LOADED;
    log_debug("loaded %s", manifest.label.c_str());
    dump();
}

void Job::unload() {
    log_debug("unloading job: %s", manifest.label.c_str());
    if (state == JOB_STATE_RUNNING) {
        log_debug("sending SIGTERM to process group %d", pid);
        if (::kill(-1 * pid, SIGTERM) < 0) {
            if (errno != ESRCH) {
                log_errno("killpg(2) of pid %d", pid);
            }
        }
        // TODO: start a timer to send a SIGKILL if it doesn't die gracefully
        //  See: https://github.com/mheily/relaunchd/issues/14
    }
    state = JOB_STATE_DEFINED;
}

bool Job::run(const std::function<void()> post_fork_cleanup) {
    struct passwd *pwent = NULL;
    struct group *grent = NULL;

    if (manifest.user_name) {
        pwent = ::getpwnam(manifest.user_name.value().c_str());
    }

    if (manifest.group_name) {
        grent = ::getgrnam(manifest.group_name.value().c_str());
    }

    auto final_env = setup_environment_variables(*this, pwent);
    ExecMonitor ipcpipe;

    pid = fork();
    if (pid < 0) {
        throw std::system_error(errno, std::system_category(), "fork(2)");
    } else if (pid == 0) {
        // This is the child process.
        ipcpipe.becomeChild();
        try {
            post_fork_cleanup();
        } catch (...) {
            log_error("post_fork_cleanup() failed");
            ipcpipe.writeStatus(ExecStatus{ExecStatus::ForkHandlerFailed});
        }
        auto maybe_error = start_child_process(*this, final_env, pwent, grent);
        if (maybe_error) {
            ipcpipe.writeStatus(*maybe_error);
        }
        exit(127);
    } else {
        // This is the parent process.
        started_at = current_time();
        log_debug("job %s started at %zu with pid %d", manifest.label.c_str(),
                  started_at.value(), pid);

        ipcpipe.becomeParent();
        ExecStatus status = ipcpipe.readStatus();
        if (status.errorCode == ExecStatus::ExecSuccess) {
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
    } else if (manifest.calendar_interval) {
        return JOB_SCHEDULE_CALENDAR;
    } else {
        return JOB_SCHEDULE_NONE;
    }
}

Job::Job(std::optional<std::filesystem::path> manifest_path_,
         Manifest manifest_)
    : manifest_path(std::move(manifest_path_)), manifest(std::move(manifest_)),
      state(JOB_STATE_DEFINED), pid(0), last_exit_status(0), term_signal(0),
      schedule(_set_schedule()) {}

bool Job::killJob(int signum) const {
    if (state != JOB_STATE_RUNNING || pid == 0) {
        log_error("tried to kill non-running job");
        return false;
    }
    if (::kill(pid, signum) < 0) {
        log_error("kill(2) of PID %d failed: %s", pid, strerror(errno));
        // TODO: gracefully handle ESRCH, if the job already died but was not
        // reaped
        return false;
    }
    log_notice("sent signal %d to process %d for job %s", signum, pid,
               manifest.label.c_str());
    return true;
}
