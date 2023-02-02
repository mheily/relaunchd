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

#pragma once

#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>

#include <nlohmann/json.hpp>
#include "manager.h"
#include "rpc_client.h"

using json = nlohmann::json;

//! A directory where temporary files can be placed.
static const inline std::string tmpdir{TMPDIR};

namespace testutil {
    static inline std::filesystem::path createManifest(const std::string &label, const json &obj) {
        std::string mpath = tmpdir + "/" + label + ".json";
        std::ofstream ofs{mpath};
        ofs << obj;
        ofs.close();
        return mpath;
    }

    static inline std::unique_ptr<Manager> getTemporaryManager() {
        Domain domain{DomainType::User, TMPDIR};
        auto mgr = std::make_unique<Manager>(domain);
        mgr->clearStateFile();
        return mgr;
    }
};

struct TestContext {
    TestContext() : mgr_impl(testutil::getTemporaryManager()), mgr(*mgr_impl) {}

    ~TestContext() {
        mgr.stopRunning();
    }

    void loadTemporaryManifest(const json &obj) {
        auto path = testutil::createManifest(obj.at("Label"), obj);
        mgr.loadManifest(obj, path);
    }

    int runLaunchctl(const std::string &method, std::vector<std::string> args) {
        auto cb = [this, &method, &args]() -> int {
            RpcClient client;
            try {
                client.invokeMethod(method, args, mgr.getDomain());
                return 0;
            } catch (...) {
                return -1;
            }
        };
        std::future<int> fp = async(std::launch::async, cb);
        mgr.handleEvent();
        return fp.get();
    }

    std::unique_ptr<Manager> mgr_impl;
    Manager &mgr;
};

class TestRunner {
public:
    void addTest(std::string name, std::function<void()> test_func) {
        dispatchTable.insert({name, test_func});
    }

    void runAllTests() {
        for (const auto &[name, test_func] : dispatchTable) {
            std::cerr << "\nRunning " << name << std::endl;
            test_func();
        }
        std::cerr << "All tests completed successfully." << std::endl;
    }

private:
    std::map<std::string, std::function<void()>> dispatchTable;
};
