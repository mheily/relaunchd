/*
 * Copyright (c) 2023 Mark Heily <mark@heily.com>
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

#include "rpc_client.h"

namespace subcommand {
void disable(Channel &chan, std::vector<std::string> &args) {
    auto kwargs = json::object({{"Label", args.at(0)}});
    json msg = json::array({"disable", kwargs});
    chan.writeMessage(msg);
    auto maybe_json = chan.readMessage();
    // FIXME
}

void enable(Channel &chan, std::vector<std::string> &args) {
    auto kwargs = json::object({{"Label", args.at(0)}});
    json msg = json::array({"enable", kwargs});
    chan.writeMessage(msg);
    auto maybe_json = chan.readMessage();
    // FIXME
}

void kill(Channel &chan, std::vector<std::string> &args) {
    auto kwargs = json::object({{"Signal", args.at(0)}, {"Label", args.at(1)}});
    json msg = json::array({"kill", kwargs});
    chan.writeMessage(msg);
    auto maybe_json = chan.readMessage();
    // FIXME
}

void list(Channel &chan, std::vector<std::string> &) {
    // FIXME: parse options
    chan.writeMessage(json::array({
        "list",
    }));
    auto msg = chan.readMessage();
    printf("%-8s %-8s %s\n", "PID", "Status", "Label");
    for (const auto &row : msg) {
        auto pid = row["PID"].get<std::string>();
        auto exit_status = row["LastExitStatus"].get<int>();
        auto label = row["Label"].get<std::string>();
        printf("%-8s %-8d %s\n", pid.c_str(), exit_status, label.c_str());
    }
}

void load(Channel &chan, std::vector<std::string> &args) {
    auto kwargs = json::object({
        {"OverrideDisabled", false},
        {"Force", false},
        {"Paths", json::array()},
    });
    for (const auto &elem : args) {
        if (elem == "-w") {
            kwargs["OverrideDisabled"] = true;
        } else if (elem == "-F") {
            kwargs["Force"] = true;
        } else {
            auto path = std::filesystem::path(elem);
            if (!std::filesystem::exists(path)) {
                // TODO: make this more informative to the user
                throw std::runtime_error("path does not exist");
            }
            kwargs["Paths"].push_back(std::filesystem::canonical(path));
        }
    }
    json msg = json::array({"load", kwargs});
    chan.writeMessage(msg);
    auto maybe_json = chan.readMessage();
    // FIXME
}

void not_implemented(Channel &, std::vector<std::string> &) {
    std::cerr << "ERROR: Not implemented yet" << std::endl;
    exit(EXIT_FAILURE);
}

void remove(Channel &chan, std::vector<std::string> &args) {
    auto kwargs = json::object({{"Label", args.at(0)}});
    json msg = json::array({"remove", kwargs});
    chan.writeMessage(msg);
    auto maybe_json = chan.readMessage();
    // FIXME
}

void start(Channel &chan, std::vector<std::string> &args) {
    auto kwargs = json::object({{"Label", args.at(0)}});
    json msg = json::array({"start", kwargs});
    chan.writeMessage(msg);
    auto maybe_json = chan.readMessage();
    // FIXME
}

void stop(Channel &chan, std::vector<std::string> &args) {
    auto kwargs = json::object({{"Label", args.at(0)}});
    json msg = json::array({"stop", kwargs});
    chan.writeMessage(msg);
    auto maybe_json = chan.readMessage();
    // FIXME
}

void submit(Channel &chan, std::vector<std::string> &args) {
    auto kwargs = json::object({{"ProgramArguments", json::array()}});
    int preamble = 1;
    for (auto it = args.begin(); it != args.end(); it++) {
        if (preamble) {
            if (*it == "-p") {
                it++;
                kwargs["Program"] = *it;
            } else if (*it == "-l") {
                it++;
                kwargs["Label"] = *it;
            } else if (*it == "-o") {
                it++;
                kwargs["StandardOutPath"] = *it;
            } else if (*it == "-e") {
                it++;
                kwargs["StandardErrorPath"] = *it;
            } else if (*it == "--") {
                preamble = 0;
            }
        } else {
            kwargs["ProgramArguments"].push_back(*it);
        }
    }
    if (!kwargs.contains("Label")) {
        throw std::runtime_error("Label is required");
    }
    json msg = json::array({"submit", kwargs});
    chan.writeMessage(msg);
    auto maybe_json = chan.readMessage();
    // FIXME
}

// TODO: deduplicate this with load()
void unload(Channel &chan, std::vector<std::string> &args) {
    auto kwargs = json::object({
        {"OverrideDisabled", false},
        {"Force", false},
        {"Paths", json::array()},
    });
    for (const auto &elem : args) {
        if (elem == "-w") {
            kwargs["OverrideDisabled"] = true;
        } else if (elem == "-F") {
            kwargs["Force"] = true;
        } else {
            auto path = std::filesystem::path(elem);
            if (!std::filesystem::exists(path)) {
                // TODO: make this more informative to the user
                throw std::runtime_error("path does not exist");
            }
            kwargs["Paths"].push_back(std::filesystem::canonical(path));
        }
    }
    json msg = json::array({"unload", kwargs});
    chan.writeMessage(msg);
    auto maybe_json = chan.readMessage();
    // FIXME
}

void version(Channel &chan, std::vector<std::string> &) {
    chan.writeMessage(json::array({"version"}));
    auto msg = chan.readMessage();
    std::cout << msg.at("version").get<std::string>() << std::endl;
}

const std::unordered_map<std::string,
                         void (*)(Channel &, std::vector<std::string> &)>
    subcommands = {
        {"disable", disable}, {"enable", enable},   {"kill", kill},
        {"list", list},       {"load", load},       {"remove", remove},
        {"start", start},     {"stop", stop},       {"submit", submit},
        {"unload", unload},   {"version", version},

        // launchd v2 API not implemented yet
        //{"print",    subcommand::not_implemented},

};
} // namespace subcommand

void RpcClient::invokeMethod(const std::string &method,
                             std::vector<std::string> &args,
                             const Domain &domain) {
    Channel chan;
    auto statedir = domain.statedir;
    chan.connect(statedir.append("rpc.sock"));
    auto funcptr = subcommand::subcommands.at(method);
    (*funcptr)(chan, args);
}

bool RpcClient::methodExists(const std::string &method) const {
    return subcommand::subcommands.count(method) > 0;
}
