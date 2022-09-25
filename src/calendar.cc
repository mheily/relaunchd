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

#include <unordered_map>

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
#include "calendar.h"
#include "job.h"

/* The main kqueue descriptor used by launchd */
static int parent_kqfd;

// key: label, val: next_scheduled_start
static std::unordered_map<std::string, time_t> calendar_jobs;

static time_t next_wakeup = 0;

static inline time_t
find_next_time(const struct manifest::cron_spec *cron, const struct tm *now)
{
	uint32_t hour, minute;

	if (cron->hour == CRON_SPEC_WILDCARD) {
		hour = now->tm_hour;
	} else {
		hour = cron->hour;
	}

	if (cron->minute == CRON_SPEC_WILDCARD) {
		minute = now->tm_min;
	} else {
		minute = cron->minute;
	}

	return ((60 * hour) + minute);
}

static inline time_t
schedule_calendar_job(Job &job)
{
	const struct manifest::cron_spec *cron = &job.manifest.calendar_interval.value();
	time_t t0 = current_time();
	struct tm tm;
	time_t result;

	localtime_r(&t0, &tm);

	/* Try to disqualify the job from running based on the current day */
	if (cron->month != CRON_SPEC_WILDCARD && cron->month != tm.tm_mon) {
		return 0;
	}
	if (cron->day != CRON_SPEC_WILDCARD && cron->day != tm.tm_mday) {
		return 0;
	}
	if (cron->weekday != CRON_SPEC_WILDCARD && cron->weekday != tm.tm_wday) {
		return 0;
	}

	/* Get the offset in minutes of the current time and the next job time,
	 * where 0 represents 00:00 of the current day.
	 */
	time_t cur_offset = (60 * tm.tm_hour) + tm.tm_min;
	time_t job_offset = find_next_time(cron, &tm);

	/* Disqualify jobs that are scheduled in the past */
	if (cur_offset > job_offset) {
		return 0;
	}

	result = job_offset - cur_offset;
	if (next_wakeup > result) {
		next_wakeup = result;
	}

	/* KLUDGE: this is ugly, b/c the manifest did not actually set StartInterval.
	 * We should really have a job_t field for this instead.
	 */
    Manifest &tmp = const_cast<Manifest&>(job.manifest);
	tmp.start_interval = result;

	log_debug("job %s scheduled to run in %ld minutes", job.manifest.label.c_str(), (long)result);

	return current_time() + (60 * result);
}

static inline void update_job_interval(Job &job)
{
    auto next_scheduled_start = schedule_calendar_job(job);
	calendar_jobs.at(job.manifest.label) = next_scheduled_start;
	log_debug("job %s will start after T=%lu", job.manifest.label.c_str(), (unsigned long)next_scheduled_start);
}

int calendar_init(int kqfd)
{
	parent_kqfd = kqfd;
	return 0;
}

int calendar_register_job(Job &job)
{
    if (job.schedule != JOB_SCHEDULE_CALENDAR) {
        throw std::logic_error("wrong job type");
    }
    // FIXME: some duplicate effort here
    auto next_scheduled_start = schedule_calendar_job(job);
    calendar_jobs.insert({job.manifest.label, next_scheduled_start});
	update_job_interval(job);
	return 0;
}

int calendar_unregister_job(Job &job)
{
	if (job.schedule != JOB_SCHEDULE_CALENDAR) {
        throw std::logic_error("wrong job type");
    }
    calendar_jobs.erase(job.manifest.label);
	return 0;
}

int calendar_handler() {
	time_t now = current_time();

	//log_debug("waking up after %u seconds", min_interval);
    for (const auto &[label, next_scheduled_start] : calendar_jobs) {
		if (now >= next_scheduled_start) {
			log_debug("job %s starting due to calendar time", label.c_str());
            auto job = manager_get_job_by_label(label);
			update_job_interval(job);
			(void) manager_wake_job(job); //FIXME: error handling
		}
	}
	return 0;
}
