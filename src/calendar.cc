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
#endif
#include <sys/types.h>
#include <ctime>

#include "clock.h"
#include "log.h"
#include "manager.h"
#include "calendar.h"
#include "job.h"

namespace calendar {
    static inline time_t
    find_next_time(const struct manifest::cron_spec &cron, const struct tm *now) {
        uint32_t hour, minute;

        if (cron.hour == CRON_SPEC_WILDCARD) {
            hour = now->tm_hour;
        } else {
            hour = cron.hour;
        }

        if (cron.minute == CRON_SPEC_WILDCARD) {
            minute = now->tm_min;
        } else {
            minute = cron.minute;
        }

        return ((60 * hour) + minute);
    }

    std::optional<std::pair<time_t, int>>
    schedule_calendar_job(struct manifest::cron_spec &cron) {
        time_t t0 = current_time();
        struct tm tm;
        time_t result;

        localtime_r(&t0, &tm);

        // XXX - FIXME -- this requires at least a daily rescheduling timer to be executed by the manager
        // otherwise the job will never be scheduled

        /* Try to disqualify the job from running based on the current day */
        if (cron.month != CRON_SPEC_WILDCARD && cron.month != tm.tm_mon) {
            return std::nullopt;
        }
        if (cron.day != CRON_SPEC_WILDCARD && cron.day != tm.tm_mday) {
            return std::nullopt;
        }
        if (cron.weekday != CRON_SPEC_WILDCARD && cron.weekday != tm.tm_wday) {
            return std::nullopt;
        }

        /* Get the offset in minutes of the current time and the next job time,
         * where 0 represents 00:00 of the current day.
         */
        time_t cur_offset = (60 * tm.tm_hour) + tm.tm_min;
        time_t job_offset = find_next_time(cron, &tm);

        /* Disqualify jobs that are scheduled in the past */
        if (cur_offset > job_offset) {
            return std::nullopt;
        }

        result = job_offset - cur_offset;

        int relative_time = 60 * (int) result;   // FIXME: 0..INT_MAX bounds checking
        time_t absolute_time = current_time() + relative_time;
        return std::make_pair(absolute_time, relative_time);
    }
}
