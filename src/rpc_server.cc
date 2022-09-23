/*
 * Copyright (c) 2022 Mark Heily <mark@heily.com>
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

#include <iostream>
#include <stdexcept>
#include <string>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "rpc_server.h"
#include "log.h"
#include "manager.h"


static void _rpc_op_list(Channel &chan, const json &) {
    // FIXME: handle Label argument
    auto msg = manager_list_jobs();
    chan.writeMessage(msg);
}

static void _rpc_op_load(Channel &chan, const json &args) {
    // FIXME: handle Force and OverrideDisabled arguments
    for (const auto &path : args[1]["Paths"]) {
        if (!std::filesystem::exists(path)) {
            chan.writeMessage("ERROR-TODO");
            return;
        }
        if (std::filesystem::is_directory(path)) {
            using std::filesystem::directory_iterator;
            for (const auto &file: directory_iterator(path)) {
                log_debug("loading %s", file.path().c_str());
                manager_load_manifest(file.path());
            }
        } else {
            std::filesystem::path p{path.get<std::string>()};
            manager_load_manifest(p);
        }
    }

    chan.writeMessage("OK");
}

static void _rpc_op_version(Channel &chan, const json &) {
    chan.writeMessage("relaunchd version unknown"); // FIXME get version number
}

// FIXME: needs a lot more error checking
int rpc_dispatch(Channel &chan) {
    static const std::unordered_map<std::string, void(*)(Channel &chan, const json &j)> handlers = {
            {"list", _rpc_op_list},
            {"load", _rpc_op_load},
            {"version", _rpc_op_version},
    };
    chan.accept();
    try {
        auto msg = chan.readMessage();
        auto method = msg.at(0).get<std::string>();
        auto funcptr = handlers.at(method);
        (*funcptr)(chan, msg);
    } catch (const std::exception &exc) {
        log_error("dispatch failed: %s", exc.what());
    }

    chan.disconnect();

    return 0;
}

