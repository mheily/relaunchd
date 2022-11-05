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
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#ifdef __FreeBSD__
#include <sys/param.h>
#endif

#include "config.h"
#include "calendar.h"
#include "job.h"
#include "log.h"
#include "manager.h"
#include "clock.h"

extern void keepalive_remove_job(const Job &job);

static int apply_resource_limits(const Job & job) {
	//TODO - SoftResourceLimits, HardResourceLimits
	//TODO - LowPriorityIO

	if (job.manifest.nice != 0) {
		if (setpriority(PRIO_PROCESS, 0, job.manifest.nice) < 0) {
			log_errno("setpriority(2) to nice=%d", job.manifest.nice);
			return (-1);
		}
	}

	return (0);
}

static inline int modify_credentials(const Job & job, const struct passwd *pwent, const struct group *grent)
{
	if (getuid() != 0) return (0);

	log_debug("setting credentials: uid=%d gid=%d", pwent->pw_uid, grent->gr_gid);

    if (job.manifest.init_groups && job.manifest.user_name) {
        if (initgroups(job.manifest.user_name.value().c_str(), grent->gr_gid) < 0) {
            log_errno("initgroups");
            return (-1);
        }
    }
	if (setgid(grent->gr_gid) < 0) {
		log_errno("setgid");
		return (-1);
	}
#if HAVE_SETLOGIN
	if (job.manifest.user_name) {
        if (setlogin(job.manifest.user_name.value().c_str()) < 0) {
            log_errno("setlogin");
            return (-1);
        }
    }
#endif
	if (setuid(pwent->pw_uid) < 0) {
		log_errno("setuid");
		return (-1);
	}
	return (0);
}


/* Add the standard set of environment variables that most programs expect.
 * See: http://pubs.opengroup.org/onlinepubs/009695399/basedefs/xbd_chap08.html
 * TODO: should cache these getenv() calls, so we don't do this dance for every
 * job invocation.
 */
static void
add_standard_environment_variables(std::vector<std::string> envvar)
{
	static const char *keys[] = { 
		"DISPLAY",
		/* Locale-related variables */
		"LC_ALL", "LC_COLLATE", "LC_CTYPE", "LC_MESSAGES", "LC_MONETARY",
		"LC_NUMERIC", "LC_TIME", "NLSPATH", "LANG",
		/* Misc */
		"TZ",
		NULL };
	const char **key = NULL, *envp = NULL;

	for (key = keys; *key != NULL; key++) {
		if ((envp = getenv(*key))) {
            auto keyval = std::string{*key} + "=" + std::string{envp};
            envvar.emplace_back(keyval);
		}
	}
}

static std::vector<std::string> setup_environment_variables(const Job & job, const struct passwd *pwent)
{
    std::vector<std::string> result;
    for (const auto &[key, val] : job.manifest.environment_variables) {
        std::string kv = std::string{key}.append("=").append(val);
        result.emplace_back(std::move(kv));
    }

	/* KLUDGE: when running as root, assume we are a system daemon and avoid adding any
	 * 	session-related variables.
	 * This is why we need a proper Domain variable for each job.
	 *
	 * The removal of these variables conforms to daemon(8) behavior on FreeBSD.
	 */
    if (pwent->pw_uid > 0) {
        if (!job.manifest.environment_variables.count("LOGNAME")) {
            result.emplace_back(std::string{"LOGNAME="} + std::string{pwent->pw_name});
        }
        if (!job.manifest.environment_variables.count("USER")) {
            result.emplace_back(std::string{"USER="} + std::string{pwent->pw_name});
        }
        if (!job.manifest.environment_variables.count("HOME")) {
            result.emplace_back(std::string{"HOME="} + std::string{pwent->pw_dir});
        }
        if (!job.manifest.environment_variables.count("PATH")) {
            result.emplace_back(std::string{"PATH=/usr/bin:/bin:/usr/local/bin"});
        }
        if (!job.manifest.environment_variables.count("SHELL")) {
            result.emplace_back(std::string{"HOME="} + std::string{pwent->pw_shell});
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

static inline int
exec_job(const Job & job, const struct passwd *pwent)
{
	int rv;
	char *path;
	char **argv, **envp;

	auto final_env = setup_environment_variables(job, pwent);
	envp = static_cast<char **>(calloc(final_env.size() + 1, sizeof(char *)));
    if (!envp) {
        throw std::bad_alloc();
    }
    for (size_t i = 0; i < final_env.size(); i++) {
        envp[i] = const_cast<char*>(final_env[i].c_str());
    }

    argv = static_cast<char **>(calloc(job.manifest.program_arguments.size() + 1, sizeof(char *)));
    if (!argv) {
        throw std::bad_alloc();
    }
    for (size_t i = 0; i < job.manifest.program_arguments.size(); i++) {
        argv[i] = const_cast<char*>(job.manifest.program_arguments[i].c_str());
    }
	if (job.manifest.program) {
		path = (char*)job.manifest.program.value().c_str();
	} else {
		path = argv[0];
	}
    if (!path) {
        throw std::logic_error("path cannot be empty");
    }
	if (job.manifest.enable_globbing) {
		//TODO: globbing
	}
	log_debug("exec: %s", path);

#if DEBUG
	log_debug("argv[]:");
	for (char **item = argv; *item; item++) {
		log_debug(" - arg: %s", *item);
	}
	log_debug("envp[]:");
	for (char **item = envp; *item; item++) {
		log_debug(" - env: %s", *item);
	}
#endif

	closelog();

	rv = execve(path, argv, envp);
	if (rv < 0) {
		log_errno("execve(2)");
		goto err_out;
    	}
	log_notice("executed job");

    free(argv);
    free(envp);
	return (0);

err_out:
    free(argv);
    free(envp);
	return -1;
}

static inline int
redirect_stdio(const Job & job)
{
	int fd;

    log_debug("setting stdin path to %s", job.manifest.stdin_path.c_str());
    fd = open(job.manifest.stdin_path.c_str(), O_RDONLY);
    if (fd < 0) goto err_out;
    if (dup2(fd, STDIN_FILENO) < 0) {
        log_errno("dup2(2)");
        (void) close(fd);
        goto err_out;
    }
    if (close(fd) < 0) goto err_out;

    log_debug("setting stdout path to %s", job.manifest.stdout_path.c_str());
    fd = open(job.manifest.stdout_path.c_str(), O_CREAT | O_WRONLY, 0600);
    if (fd < 0) goto err_out;
    if (dup2(fd, STDOUT_FILENO) < 0) {
        log_errno("dup2(2)");
        (void) close(fd);
        goto err_out;
    }
    if (close(fd) < 0) goto err_out;

    log_debug("setting stderr path to %s", job.manifest.stderr_path.c_str());
    fd = open(job.manifest.stderr_path.c_str(), O_CREAT | O_WRONLY, 0600);
    if (fd < 0) goto err_out;
    if (dup2(fd, STDERR_FILENO) < 0) {
        log_errno("dup2(2)");
        (void) close(fd);
        goto err_out;
    }
    if (close(fd) < 0) goto err_out;

	return 0;

err_out:
	return -1;
}

static int
start_child_process(const Job &job, const struct passwd *pwent, const struct group *grent)
{
#ifndef NOFORK
	if (setsid() < 0) {
		log_errno("setsid");
		goto err_out;
	}
#endif
	if (apply_resource_limits(job) < 0) {
		log_error("unable to apply resource limits");
		goto err_out;
	}
	if (job.manifest.working_directory) {
        auto dir = job.manifest.working_directory.value().c_str();
		if (chdir(dir) < 0) {
			log_error("unable to chdir to %s", dir);
			goto err_out;
		}
	}
	if (job.manifest.root_directory && getuid() == 0) {
        auto dir = job.manifest.root_directory.value().c_str();
        if (chroot(dir) < 0) {
			log_error("unable to chroot to %s", dir);
			goto err_out;
		}
	}
	if (getuid() == 0 && modify_credentials(job, pwent, grent) < 0) {
		log_error("unable to modify credentials");
		goto err_out;
	}

    // FIXME: convert umask to octal
    //(void) umask(job.manifest.umask);

    if (redirect_stdio(job) < 0) {
		log_error("unable to redirect stdio");
		goto err_out;
	}

	if (exec_job(job, pwent) < 0) {
		log_error("exec_job() failed");
		goto err_out;
	}

	return (0);

err_out:
	log_error("job %s failed to start; see previous log message for details", job.manifest.label.c_str());
	return (-1);
}

void Job::load() {
	/* TODO: This is the place to setup on-demand watches for the following keys:
			WatchPaths
			QueueDirectories
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
			log_errno("killpg(2) of pid %d", pid);
			/* not sure how to handle the error, we still want to clean up */
		}
		state = JOB_STATE_KILLED;
		//TODO: start a timer to send a SIGKILL if it doesn't die gracefully
        // See: https://github.com/mheily/relaunchd/issues/14
	} else {
		//TODO: update the timer interval in timer.c?
		state = JOB_STATE_DEFINED;
	}
    // FIXME: should the entire job disappear from the jobs table?
}

void Job::run() {
    struct passwd *pwent = NULL;
	struct group *grent = NULL;

    if (manifest.user_name) {
        pwent = ::getpwnam(manifest.user_name.value().c_str());
    } else {
        pwent = ::getpwuid(getuid());
    }
	if (!pwent) {
        throw std::system_error(errno, std::system_category(), "no pwent");
	}

    if (manifest.group_name) {
        grent = ::getgrnam(manifest.group_name.value().c_str());
    } else {
        grent = ::getgrgid(getgid());
    }
	if (!grent) {
        throw std::system_error(errno, std::system_category(), "no grent");
	}

	// temporary for debugging
#ifdef NOFORK
	(void) start_child_process(*this, pwent, grent);
#else
	pid = fork();
	if (pid < 0) {
        throw std::system_error(errno, std::system_category(), "fork(2)");
	} else if (pid == 0) {
		if (start_child_process(*this, pwent, grent) < 0) {
			//TODO: report failures to the parent
			exit(124);
		}
	} else {
        started_at = current_time();
		log_debug("job %s started at %zu with pid %d", manifest.label.c_str(), started_at, pid);
		state = JOB_STATE_RUNNING;
        // FIXME: sockets
        ///struct job_manifest_socket *jms;
//		SLIST_FOREACH(jms, &job.manifest.sockets, entry) {
//			job_manifest_socket_close(jms);
//		}
	}
#endif /* NOFORK */
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

Job::Job(std::optional<std::filesystem::path> manifest_path_, Manifest manifest_) :
        manifest_path(std::move(manifest_path_)),
        manifest(std::move(manifest_)),
        state(JOB_STATE_DEFINED),
        pid(0),
        last_exit_status(0),
        term_signal(0),
        schedule(_set_schedule()){}

bool Job::kill(int signum) const {
    if (!isRunning()) {
        log_debug("tried to kill non-running job");
        return false;
    }
    if (::kill(pid, signum) < 0) {
        log_error("kill(2) of PID %d failed: %s", pid, strerror(errno));
        // TODO: gracefully handle ESRCH, if the job already died but was not reaped
        return false;
    }
    return true;
}

bool Job::kill(const std::string &signame_or_num) {
    try {
        return kill(std::stoi(signame_or_num));
    } catch (...) {
        // FIXME: need to do something like this for Linux, which does not have sys_signame
#if HAVE_SYS_SIGNAME
        auto s = signame_or_num;
        if (s.find("sig") == 0 || s.find("SIG") == 0) {
            s = s.substr(3);
        }
        for (int signum = 1; signum < NSIG; signum++) {
            if (!strcasecmp(s.c_str(), sys_signame[signum])) {
                return kill(signum);
            }
        }
#endif
    }
    log_debug("tried to send unknown signal by name");
    return false;
}
