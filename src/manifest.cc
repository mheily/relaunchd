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
#include <grp.h>
#include <pwd.h>
#include <unistd.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "../vendor/tinyxml2.h"

#include "log.h"
#include "manifest.h"

namespace manifest {
class InvalidManifestError : public std::exception {
  public:
    const char *what() const throw() { return "Invalid manifest"; }
};

class NotSupportedError : public std::exception {
  public:
    const char *what() const throw() { return "Not currently supported"; }
};

///** Parse a field within a crontab(5) specification */
// static int32_t parse_cron_field(json &obj, const char *key, int64_t start,
//                                 int64_t end) {
//     if (obj.contains(key)) {
//         int32_t val;
//         obj.at(key).get_to(val);
//         if (val < start || val > end) {
//             throw std::range_error(key);
//         }
//         return val;
//     } else {
//         return CRON_SPEC_WILDCARD;
//     }
// }

// static struct cron_spec parse_start_calendar_interval(json &obj) {
//     struct cron_spec cron = {
//         .minute = parse_cron_field(obj, "Minute", 0, 59),
//         .hour = parse_cron_field(obj, "Hour", 0, 23),
//         .day = parse_cron_field(obj, "Day", 1, 31),
//         .weekday = parse_cron_field(obj, "Weekday", 0, 7),
//         .month = parse_cron_field(obj, "Month", 1, 12),
//     };
//     /* Normalize Sunday to always be 0 */
//     if (cron.weekday == 7)
//         cron.weekday = 0;
//     return cron;
// }

void from_json(const json &j, Manifest &m) {
    if (j.contains("Label")) {
        std::string tmp;
        j.at("Label").get_to(tmp);
        m.label = tmp;
    }
    if (j.contains("UserName")) {
        std::string tmp;
        j.at("UserName").get_to(tmp);
        m.user_name = std::move(tmp);
    }
    if (j.contains("GroupName")) {
        std::string tmp;
        j.at("GroupName").get_to(tmp);
        m.group_name = std::move(tmp);
    }
    if (j.contains("Program")) {
        std::string tmp;
        j.at("Program").get_to(tmp);
        m.program = tmp;
    }
    if (j.contains("ProgramArguments")) {
        j.at("ProgramArguments").get_to(m.program_arguments);
    }
    if (j.contains("EnableGlobbing")) {
        // j.at("EnableGlobbing").get_to(m.enable_globbing);
        throw NotSupportedError();
    }
    if (j.contains("RunAtLoad")) {
        j.at("RunAtLoad").get_to(m.run_at_load);
    }
    if (j.contains("WorkingDirectory")) {
        std::string tmp;
        j.at("WorkingDirectory").get_to(tmp);
        m.working_directory = std::move(tmp);
    }
    if (j.contains("RootDirectory")) {
        std::string tmp;
        j.at("RootDirectory").get_to(tmp);
        m.root_directory = std::move(tmp);
    }
    if (j.contains("EnvironmentVariables")) {
        j.at("EnvironmentVariables").get_to(m.environment_variables);
    }
    if (j.contains("Umask")) {
        auto elem = j.at("Umask");
        mode_t value;
        if (elem.type() == json::value_t::number_integer) {
            elem.get_to(value);
        } else if (elem.type() == json::value_t::string) {
            std::string buf;
            elem.get_to(buf);
            value = std::stoul(buf, nullptr, 8);
        } else {
            throw std::runtime_error("unsupported Umask type");
        }
        m.umask = value;
    }
    if (j.contains("Disabled")) {
        j.at("Disabled").get_to(m.disabled);
    }
    if (j.contains("ExitTimeout")) {
        uint32_t tmp;
        j.at("ExitTimeout").get_to(tmp);
        m.exit_timeout = std::chrono::seconds{tmp};
    }
    if (j.contains("StartInterval")) {
        uint32_t tmp;
        j.at("StartInterval").get_to(tmp);
        m.start_interval = tmp;
    }
    if (j.contains("ThrottleInterval")) {
        j.at("ThrottleInterval").get_to(m.throttle_interval);
    }
    if (j.contains("Nice")) {
        uint32_t tmp;
        j.at("Nice").get_to(tmp);
        m.nice = tmp;
    }
    if (j.contains("InitGroups")) {
        j.at("InitGroups").get_to(m.init_groups);
    }
    if (j.contains("WatchPaths")) {
        // j.at("WatchPaths").get_to(m.watch_paths);
        throw NotSupportedError();
    }
    if (j.contains("QueueDirectories")) {
        // j.at("QueueDirectories").get_to(m.queue_directories);
        throw NotSupportedError();
    }
    if (j.contains("StartOnMount")) {
        // j.at("StartOnMount").get_to(m.start_on_mount);
        throw NotSupportedError();
    }
    if (j.contains("StandardInPath")) {
        j.at("StandardInPath").get_to(m.stdin_path);
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
        //        auto obj = j.at("StartCalendarInterval");
        //        m.calendar_interval = parse_start_calendar_interval(obj);
        throw NotSupportedError();
    }
    if (j.contains("KeepAlive")) {
        auto keepalive = j.at("KeepAlive");
        if (keepalive.type() == json::value_t::boolean) {
            keepalive.get_to(m.keep_alive.always);
        } else if (keepalive.type() == json::value_t::object) {
            if (keepalive.contains("Always")) {
                keepalive.at("Always").get_to(m.keep_alive.always);
            }
        }
    }
    if (j.contains("Sockets")) {
        throw NotSupportedError();
    }
    m.rectify();
    if (!m.validate()) {
        throw InvalidManifestError();
    }
}

#if XML_MANIFEST_SUPPORT
// FIXME: needs work, see https://github.com/mheily/relaunchd/issues/16
std::optional<json> parse_xml(const char *path) {
    using namespace tinyxml2;
    XMLError xmlerr;
    XMLDocument doc;
    xmlerr = doc.LoadFile(path);
    if (xmlerr != XML_SUCCESS) {
        log_error("LoadFile failed: %d", xmlerr);
        return std::nullopt;
    }

    // FIXME: lots of error checking needed
    json result;
    XMLElement *topElement =
        doc.FirstChildElement("plist")->FirstChildElement("dict");
    if (topElement->NoChildren()) {
        log_error("topElement has no child nodes");
        return std::nullopt;
    }
    for (auto elem = topElement->FirstChildElement(); elem;
         elem = elem->NextSiblingElement()) {
        if (strcasecmp(elem->Name(), "key") != 0) {
            log_error("expected key");
            return std::nullopt;
        }
        auto key = elem->GetText();
        if (!key) {
            return std::nullopt; // todo: log
        }
        printf("got:%s %s\n", elem->Name(), key);

        elem = elem->NextSiblingElement();
        if (!elem) {
            log_error("expected a sibling element");
            return std::nullopt;
        }

        auto valtype = elem->Name();
        if (!valtype) {
            log_error("unable to get type of value");
            return std::nullopt;
        }
        if (strcasecmp(valtype, "string") == 0) {
            auto val = elem->GetText();
            if (!val) {
                return std::nullopt; // todo: log
            }
            printf("got: %s=%s\n", key, val);
            result[key] = val;
        } else if (strcasecmp(valtype, "true") == 0) {
            result[key] = true;
        } else if (strcasecmp(valtype, "false") == 0) {
            result[key] = false;
        } else {
            log_error("unsupported value type");
            return std::nullopt;
        }
    }
    return std::make_optional<json>(result);
}
#endif // XML_MANIFEST_SUPPORT

json parse(const std::filesystem::path &path) {
#if XML_MANIFEST_SUPPORT
    if (path.extension() == ".plist") {
        auto maybe_obj = manifest::parse_xml(path.c_str());
        if (maybe_obj) {
            return maybe_obj.value();
        } else {
            log_error("failed to parse plist as XML");
            return -1;
        }
    }
#endif // XML_MANIFEST_SUPPORT

    if (path.extension() == ".json") {
        std::ifstream ifs{path};
        return json::parse(ifs);
    } else {
        log_error("unable to parse: invalid file extension");
        throw std::runtime_error("parse failed");
    }
}

void Manifest::rectify() {
    if (!program && program_arguments.empty()) {
        // TODO: convert to ManifestError
        throw std::logic_error(
            "one of these must be set: Program and/or ProgramArguments");
    } else if (!program) {
        program = program_arguments[0];
    } else if (program_arguments.empty()) {
        program_arguments.emplace_back(program.value());
    }
}

bool Manifest::validate() {
    if (!static_cast<const std::string>(label).size()) {
        log_error("job does not have a label");
    } else if (!program && !program_arguments.empty()) {
        // TODO: deduplicate this with rectify()
        log_error("job does not set Program or ProgramArguments");
        //    } else if (calendar_interval && start_interval) {
        //        log_error("job %s has both a calendar and a non-calendar
        //        interval",
        //                  label.c_str());
    } else if (group_name && !user_name) {
        log_error("job %s sets GroupName but does not provide UserName",
                  label.c_str());
    } else {
        return true;
    }
    return false;
}
} // namespace manifest
