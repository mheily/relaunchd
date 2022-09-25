/*
 * Copyright (c) 2015-22 Mark Heily <mark@heily.com>
 * Copyright (c) 2015 Steve Gerbino <steve@gerbino.co>
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

#include <fstream>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>

#include "../vendor/json.hpp"
using json = nlohmann::json;

#include "manifest.h"
#include "log.h"

namespace manifest {
    class InvalidManifestError : public std::exception {
    public:
        const char *what() const throw() {
            return "Invalid manifest";
        }
    };

    /** Parse a field within a crontab(5) specification */
    static int32_t
    parse_cron_field(json& obj, const char *key, int64_t start, int64_t end)
    {
        if (obj.contains(key)) {
            int32_t val;
            obj.at(key).get_to(val);
            if (val < start || val > end) {
                throw std::range_error(key);
            }
            return val;
        } else {
            return CRON_SPEC_WILDCARD;
        }
    }

    static struct cron_spec parse_start_calendar_interval(json& obj)
    {
        struct cron_spec cron = {
                .minute = parse_cron_field(obj, "Minute", 0, 59),
                .hour = parse_cron_field(obj, "Hour", 0, 23),
                .day = parse_cron_field(obj, "Day", 1, 31),
                .weekday = parse_cron_field( obj, "Weekday", 0, 7),
                .month = parse_cron_field(obj, "Month", 1, 12),
        };
        /* Normalize Sunday to always be 0 */
        if (cron.weekday == 7)
            cron.weekday = 0;
        return cron;
    }

    void from_json(const json& j, Manifest& m) {
        j.at("Label").get_to(m.label);
        if (j.contains("UserName")) {
            j.at("UserName").get_to(m.user_name.value());
        }
        if (j.contains("GroupName")) {
            j.at("GroupName").get_to(m.user_name.value());
        }
        if (j.contains("Program")) {
            j.at("Program").get_to(m.program.value());
        }
        if (j.contains("ProgramArguments")) {
            j.at("ProgramArguments").get_to(m.program_arguments);
        }
        if (j.contains("EnableGlobbing")) {
            j.at("EnableGlobbing").get_to(m.enable_globbing);
        }
        if (j.contains("RunAtLoad")) {
            j.at("RunAtLoad").get_to(m.run_at_load);
        }
        if (j.contains("WorkingDirectory")) {
            j.at("WorkingDirectory").get_to(m.working_directory.value());
        }
        if (j.contains("RootDirectory")) {
            j.at("RootDirectory").get_to(m.root_directory.value());
        }
        if (j.contains("EnvironmentVariables")) {
            j.at("EnvironmentVariables").get_to(m.environment_variables);
        }
        if (j.contains("Umask")) {
            j.at("Umask").get_to(m.umask.value());
        }
        if (j.contains("Timeout")) {
            j.at("Timeout").get_to(m.timeout);
        }
        if (j.contains("ExitTimeout")) {
            j.at("ExitTimeout").get_to(m.exit_timeout);
        }
        if (j.contains("StartInterval")) {
            j.at("StartInterval").get_to(m.start_interval);
        }
        if (j.contains("ThrottleInterval")) {
            j.at("ThrottleInterval").get_to(m.throttle_interval);
        }
        if (j.contains("Nice")) {
            j.at("Nice").get_to(m.nice);
        }
        if (j.contains("InitGroups")) {
            j.at("InitGroups").get_to(m.init_groups);
        }
        if (j.contains("WatchPaths")) {
            j.at("WatchPaths").get_to(m.watch_paths);
        }
        if (j.contains("QueueDirectories")) {
            j.at("QueueDirectories").get_to(m.queue_directories);
        }
        if (j.contains("StartOnMount")) {
            j.at("StartOnMount").get_to(m.start_on_mount);
        }
        if (j.contains("StandardInPath")) {
            j.at("StandardInPath").get_to(m.stdin_path);
        }
        if (j.contains("StandardOutPath")) {
            j.at("StandardOutPath").get_to(m.stdout_path);
        }
        if (j.contains("StandardOutPath")) {
            j.at("StandardOutPath").get_to(m.stdout_path);
        }
        if (j.contains("StandardErrorPath")) {
            j.at("StandardErrorPath").get_to(m.stderr_path);
        }
        if (j.contains("AbandonProcessGroup")) {
            j.at("AbandonProcessGroup").get_to(m.abandon_process_group);
        }
        if (j.contains("StartCalendarInterval")) {
            auto obj = j.at("StartCalendarInterval");
            m.calendar_interval = parse_start_calendar_interval(obj);
        }
        if (j.contains("KeepAlive")) {
            auto keepalive = j.at("KeepAlive");
            if (keepalive.contains("Always")) {
                keepalive.at("Always").get_to(m.keep_alive.always);
            }
        }
        m.rectify();
        if (!m.validate()) {
            throw InvalidManifestError();
        }
    }

    void Manifest::rectify() {
        struct passwd *pwent;
        struct group *grent;

        // XXX-FIXME this is a bad idea, just use the LaunchAgent and LaunchDaemon directory structure
        //      and be explicit.
        /* Undocumented heuristic to decide if it is an agent:
         *  - agents cannot set the User property
         *  - daemons must set the User and Group property
         */
        job_is_agent = (!user_name && !group_name);

        auto uid = ::geteuid();
        if (uid == 0) {
            if (!user_name) {
                user_name = "root";  // FIXME: use /etc/passwd|group for this
            }
            if (!group_name) {
                group_name = "wheel";  // FIXME: use /etc/passwd/group for this
            } else {
                pwent = ::getpwuid(uid);
                if (!pwent) {
                    throw std::system_error(errno, std::system_category(), "no pwent");
                }
                // FIXME: should we fail if User is already set?
                user_name = std::string{pwent->pw_name};

                grent = ::getgrgid(getegid());
                if (!grent) {
                    throw std::system_error(errno, std::system_category(), "no grent");
                }
                // FIXME: should we fail if Group is already set?
                group_name = std::string{grent->gr_name};
            }

            //
            if (!program && program_arguments.empty()) {
                // TODO: convert to ManifestError
                throw std::logic_error("one of these must be set: Program and/or ProgramArguments");
            } else if (!program) {
                program = program_arguments[0];
            } else if (program_arguments.empty()) {
                program_arguments.emplace_back(program.value());
            }
        }
    }

    bool Manifest::validate() {
        if (!label.size()) {
            log_error("job does not have a label");
            return false;
        }

        if (!program && !program_arguments.empty()) {
            // TODO: deduplicate this with rectify()
            log_error("job does not set Program or ProgramArguments");
            return false;
        }

        if (!user_name) {
            log_error("job %s does not set `user_name'", label.c_str());
            return false;
        }

        if (!group_name) {
            log_error("job %s does not set `group_name'", label.c_str());
            return false;
        }

        if (calendar_interval && start_interval) {
            log_error("job %s has both a calendar and a non-calendar interval",
                      label.c_str());
            return false;
        }
        return true;
    }
} // namespace manifest
