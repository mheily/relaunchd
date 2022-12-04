/*
 * Copyright (c) 2015 Mark Heily <mark@heily.com>
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

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "dependency.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

//! The default value of ExitTimeout, in seconds
#define DEFAULT_EXIT_TIMEOUT 20

//! A unique identifier for a job
class Label {
  private:
    std::string value;

  public:
    Label() : value(std::string{""}) {}

    Label(std::string s) : value(std::move(s)) {
        if (value.empty()) {
            throw std::runtime_error("Label cannot be an empty string");
        }
        if (value.find('/') != value.npos) {
            throw std::runtime_error(
                "Labels may not include the '/' character");
        }
    }

    const char *c_str() const {
        assert(!value.empty());
        return value.c_str();
    }

    explicit operator const std::string() const {
        assert(!value.empty());
        return value;
    }

    explicit operator const char *() const {
        assert(!value.empty());
        return value.c_str();
    }
};

namespace manifest {
/** A wildcard value in a crontab(5) specification */
#define CRON_SPEC_WILDCARD INT32_MAX

struct cron_spec {
    int32_t minute;
    int32_t hour;
    int32_t day;
    int32_t weekday;
    int32_t month;
};

struct Manifest {
    Label label;

    std::optional<std::string> user_name;
    std::optional<std::string> group_name;

    /// TODO: Make this std::variant
    std::optional<std::string> program;
    std::vector<std::string> program_arguments;
    ///

    bool disabled = false;
    bool enable_globbing;
    bool run_at_load;
    std::optional<std::string> working_directory;
    std::optional<std::string> root_directory;

    std::unordered_map<std::string, std::string> environment_variables;

    std::optional<std::string> umask = "022";
    uint32_t timeout; // DEPRECATED -- remove this
    std::chrono::seconds exit_timeout =
        std::chrono::seconds{DEFAULT_EXIT_TIMEOUT};
    std::optional<uint32_t> start_interval;
    uint32_t throttle_interval = 10;
    std::optional<uint32_t> nice;
    bool init_groups = true;
    std::vector<std::string> watch_paths;
    std::vector<std::string> queue_directories;
    bool start_on_mount = false;
    std::string stdin_path = "/dev/null";
    std::string stdout_path = "/dev/null";
    std::string stderr_path = "/dev/null";
    bool abandon_process_group = true;
    std::optional<struct cron_spec> calendar_interval;
    struct {
        bool always = false; /* Equivalent to setting { "KeepAlive": true } */
                             /* TODO: various other conditions */
    } keep_alive;

    DependencyList dependencies;

    // TODO: ResourceLimits, HopefullyExits*, inetd, LowPriorityIO,
    // LaunchOnlyOnce SLIST_HEAD(,job_manifest_socket) sockets;

    void rectify();
    bool validate();
    //        mode_t getUmask() {
    //            // FIXME: something like
    //            //result = sscanf(umask, "%hi", (unsigned short *)
    //            &manifest->umask);
    //        }
};

void from_json(const json &j, Manifest &m);

#if XML_MANIFEST_SUPPORT
std::optional<json> parse_xml(const char *path);
#endif

json parse(const std::filesystem::path &);
} // namespace manifest

using Manifest = manifest::Manifest;
