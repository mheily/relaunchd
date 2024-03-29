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

#include "config.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "manager.h"
#include "rpc_server.h"

static json _rpc_op_disable(const json &args, Manager &mgr) {
    Label label{args[1]["Label"]};
    mgr.overrideJobEnabled(label, false);
    return {{"error", false}};
}

static json _rpc_op_enable(const json &args, Manager &mgr) {
    Label label{args[1]["Label"]};
    mgr.overrideJobEnabled(label, true);
    return {{"error", false}};
}

static json _rpc_op_kill(const json &args, Manager &mgr) {
    const Label label{args[1]["Label"]};
    const std::string &signame_or_num = args[1]["Signal"];
    return {{"error", mgr.killJob(label, signame_or_num)}};
}

static json _rpc_op_list(const json &, Manager &mgr) {
    // FIXME: handle Label argument
    return mgr.listJobs();
}

static json _rpc_op_load(const json &args, Manager &mgr) {
    bool forceLoad = args[1]["Force"];
    bool overrideDisabled = args[1]["OverrideDisabled"];
    bool error = false;
    for (const auto &load_path : args[1]["Paths"]) {
        if (!mgr.loadAllManifests(load_path, overrideDisabled, forceLoad)) {
            error = true;
        }
    }
    mgr.startRunning();
    return {{"error", error}};
}

static json _rpc_op_unload(const json &args, Manager &mgr) {
    bool forceUnload = args[1]["Force"];
    bool overrideDisabled = args[1]["OverrideDisabled"];
    bool is_error = false;
    for (const auto &jsonobj : args[1]["Paths"]) {
        const std::filesystem::path path{jsonobj.get<std::string>()};
        if (!std::filesystem::exists(path)) {
            log_warning("path does not exist: %s", path.c_str());
            is_error = true;
            break;
        }
        if (std::filesystem::is_directory(path)) {
            using std::filesystem::directory_iterator;
            for (const auto &file : directory_iterator(path)) {
                if (!mgr.unloadJob(file.path(), overrideDisabled,
                                   forceUnload)) {
                    log_warning("unload failed: %s", file.path().c_str());
                    is_error = true;
                    break;
                }
            }
        } else {
            if (!mgr.unloadJob(path, overrideDisabled, forceUnload)) {
                log_warning("unload failed: %s", path.c_str());
                is_error = true;
                break;
            }
        }
    }
    return {{"error", is_error}};
}

/* FIXME: will need to ask the manager to start/stop the job,
 * instead of directly controlling the job.
 */
#if 0
static json _rpc_op_start(const json &args, Manager &mgr) {
    const std::string &label = args[1]["Label"];
    if (mgr.jobExists(label)) {
        auto & job = mgr.getJob(label);
        job.run();
        return {{"error", false}};
    } else {
        return {{"error", true}};
    }
}

static json _rpc_op_stop(const json &args, Manager &mgr) {
    const std::string &label = args[1]["Label"];
    if (mgr.jobExists(label)) {
        auto & job = mgr.getJob(label);
        job.kill("SIGTERM");
        return {{"error", false}};
    } else {
        return {{"error", true}};
    }
}
#endif

static json _rpc_op_remove(const json &args, Manager &mgr) {
    const Label label{args[1]["Label"]};
    bool status = mgr.unloadJob(label);
    // FIXME: should return immediately, not waiting for job to be unloaded
    return {{"error", status}};
}

static json _rpc_op_submit(const json &args, Manager &mgr) {
    std::string path = "/dev/null";
    bool ok = mgr.loadManifest(args[1], path);
    mgr.startRunning();
    return {{"error", ok}};
}

static json _rpc_op_version(const json &, Manager &) {
    static const auto version = std::string{"relaunch version "} +
                                std::string{relaunch::config::VERSION};
    return {{"error", false}, {"version", version}};
}

// FIXME: needs a lot more error checking
int rpc_dispatch(Channel &chan, Manager &mgr) {
    static const std::unordered_map<std::string,
                                    json (*)(const json &, Manager &)>
        handlers = {
            {"disable", _rpc_op_disable},
            {"enable", _rpc_op_enable},
            {"kill", _rpc_op_kill},
            {"list", _rpc_op_list},
            {"load", _rpc_op_load},
            {"remove", _rpc_op_remove},
            // FIXME:{"start", _rpc_op_start},
            // FIXME:{"stop", _rpc_op_stop},
            {"submit", _rpc_op_submit},
            {"unload", _rpc_op_unload},
            {"version", _rpc_op_version},
        };
    chan.accept();
    try {
        auto msg = chan.readMessage();
        auto method = msg.at(0).get<std::string>();
        auto funcptr = handlers.at(method);
        json response;
        try {
            response = (*funcptr)(msg, mgr);
        } catch (const std::exception &exc) {
            log_error("unhandled exception in %s(): %s", method.c_str(),
                      exc.what());
            response = {{"error", true}};
        }
        chan.writeMessage(response);
    } catch (const std::exception &exc) {
        log_error("dispatch failed: %s", exc.what());
    }

    chan.disconnect();

    return 0;
}
