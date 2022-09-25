/*
 * Copyright (c) 2016 Mark Heily <mark@heily.com>
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

#include <fcntl.h>
#include <sys/types.h>
#include <sys/event.h>
#include <limits.h>
#include <unistd.h>

#include "clock.h"
#include "log.h"
#include "keepalive.h"
#include "job.h"

/* The main kqueue descriptor used by launchd */
static int parent_kqfd;

struct Watchdog {
    Watchdog(const Job &job) {
        label = job.manifest.label;
        restart_after = current_time() + job.manifest.throttle_interval;
    }

	std::string label;
	time_t restart_after; /* After this walltime, the job should be restarted */
};

static std::unordered_map<std::string, Watchdog> watchdog_list;

static void update_wake_interval();

int keepalive_init(int kqfd)
{
	parent_kqfd = kqfd;
	return 0;
}

void keepalive_add_job(Job &job)
{
	if (!job.manifest.keep_alive.always) {
        throw std::logic_error("keepalive is disabled");
    }
    watchdog_list.insert({job.manifest.label, Watchdog{job}});
    log_debug("job `%s' will be automatically restarted in %d seconds",
            job.manifest.label.c_str(), job.manifest.throttle_interval);
    update_wake_interval();
}

void keepalive_remove_job(const Job &job) {
    watchdog_list.erase(job.manifest.label);
	update_wake_interval();
}

void keepalive_wake_handler(void)
{
	time_t now = current_time();

	log_debug("watchdog handler running");
	update_wake_interval();

    for (auto it = watchdog_list.begin(); it != watchdog_list.end(); ++it) {
        auto &watchdog = it->second;
		if (now >= watchdog.restart_after) {

            // FIXME: need to get job from manager

//			if (cur->job->state != JOB_STATE_RUNNING) {
//				log_debug("job `%s' restarted due to KeepAlive mechanism", cur->job->jm.label.c_str());
//				job_run(cur->job);
//			}

		}
	}
}

static void update_wake_interval()
{
	struct kevent kev;
	int next_wake_time;   // FIXME: using int creates a narrowing warning
	static int interval = 0;

	if (watchdog_list.empty()) {
		EV_SET(&kev, JOB_SCHEDULE_KEEPALIVE, EVFILT_TIMER, EV_ADD | EV_DISABLE, 0, 0, reinterpret_cast<void *>(&keepalive_wake_handler));
		if (kevent(parent_kqfd, &kev, 1, NULL, 0, NULL) < 0) {
			err(1, "kevent(2)");
		}
		log_debug("disabling keepalive polling; no more watchdogs");
	} else {
		next_wake_time = INT_MAX;
        for (auto &[label, watchdog] : watchdog_list) {
            if (watchdog.restart_after < next_wake_time)
                next_wake_time = watchdog.restart_after;
        }
		int time_delta = (next_wake_time - current_time()) * 1000;
		if (time_delta <= 0)
			time_delta = 10000;
		if (interval != time_delta) {
			EV_SET(&kev, JOB_SCHEDULE_KEEPALIVE, EVFILT_TIMER,
						EV_ADD | EV_ENABLE, 0, time_delta, reinterpret_cast<void *>(&keepalive_wake_handler));
			if (kevent(parent_kqfd, &kev, 1, NULL, 0, NULL) < 0) {
					err(1, "kevent(2)");
			}

			log_debug("scheduled next wakeup event in %d ms", time_delta);
			interval = time_delta;
		}
	}
}
