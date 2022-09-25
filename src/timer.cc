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

#if HAVE_SYS_LIMITS_H
#include <sys/limits.h>
#else
#include <limits.h>
#endif
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <time.h>

#include "clock.h"
#include "log.h"
#include "manager.h"
#include "timer.h"
#include "job.h"

/* The main kqueue descriptor used by launchd */
static int parent_kqfd;

// key: label, val: next_scheduled_start
static std::unordered_map<std::string, time_t> start_interval_jobs;

static uint32_t min_interval = UINT_MAX;

/* Find the smallest interval that we can wait before waking up at least one job */
static void update_min_interval()
{
	struct kevent kev;
	int saved_interval = min_interval;

	if (start_interval_jobs.empty()) {
		if (min_interval == 0) {
			return;
		}
		EV_SET(&kev, JOB_SCHEDULE_PERIODIC, EVFILT_TIMER, EV_ADD | EV_DISABLE, 0, 0, reinterpret_cast<void*>(&setup_timers));
		if (kevent(parent_kqfd, &kev, 1, NULL, 0, NULL) < 0) {
			err(1, "kevent(2)");
		}
		min_interval = 0;
	} else {
        for (auto & [label, start_interval] : start_interval_jobs) {
			if (start_interval < min_interval)
				min_interval = start_interval;
		}
		if (min_interval > 0 && saved_interval == 0) {
			EV_SET(&kev, JOB_SCHEDULE_PERIODIC, EVFILT_TIMER,
					EV_ADD | EV_ENABLE, 0, (1000 * min_interval), reinterpret_cast<void*>(&setup_timers));
			if (kevent(parent_kqfd, &kev, 1, NULL, 0, NULL) < 0) {
				err(1, "kevent(2)");
			}
		}
	}
}

static inline void update_job_interval(Job & job)
{
	auto next_scheduled_start = current_time() + job.manifest.start_interval;
    start_interval_jobs.at(job.manifest.label) = next_scheduled_start;
	log_debug("job %s will start after T=%lu", job.manifest.label.c_str(), (unsigned long)next_scheduled_start);
}

int setup_timers(int kqfd)
{
	parent_kqfd = kqfd;
	return 0;
}

void timer_register_job(Job &job) {
    // TODO: verify this is a timer job
    auto next_scheduled_start = current_time() + job.manifest.start_interval;
    start_interval_jobs.insert({job.manifest.label, next_scheduled_start});
	update_min_interval();
}

void timer_unregister_job(Job &job)
{
    // TODO: verify this is a timer job
    start_interval_jobs.erase(job.manifest.label);
	update_min_interval();
}

int timer_handler()
{
	time_t now = current_time();

	log_debug("waking up after %u seconds", min_interval);
    for (const auto &[label, next_scheduled_start] : start_interval_jobs) {
		if (now >= next_scheduled_start) {
			log_debug("job %s starting due to timer interval", label.c_str());
            auto job = manager_get_job_by_label(label);
            update_job_interval(job);
			(void) manager_wake_job(job); //FIXME: error handling
		}
	}
	return 0;
}
