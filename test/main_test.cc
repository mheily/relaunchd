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

#include "common.hpp"
#include "../src/log.h"

extern void addLaunchctlTests(TestRunner &runner);
extern void addManagerTests(TestRunner &runner);
extern void addManifestTests(TestRunner &runner);
extern void addStateFileTests(TestRunner &runner);

void test_usage() {
    std::cout << "TODO: usage" << std::endl;
}

int test_main(int argc, char *argv[]) {
    int c;
    while ((c = getopt(argc, argv, "v")) != -1) {
        switch (c) {
            case 'v':
                log_freopen(stderr);
                break;
            default:
                test_usage();
                break;
        }
    }

    std::vector<std::string> positional_args;
    for (int i = optind; i < argc; i++) {
        positional_args.emplace_back(argv[i]);
    }

    TestRunner runner;
    std::unordered_map<std::string, std::function<void(TestRunner &)>> tests = {
            {"Launchctl", addLaunchctlTests},
            {"Manager", addManagerTests},
            {"Manifest", addManifestTests},
            {"StateFile", addStateFileTests},
    };
    for (const auto &[key, func] : tests) {
        auto& vec = positional_args;
        if (vec.empty() || std::find(vec.begin(), vec.end(), key) != vec.end()) {
            func(runner);
        }
    }
    runner.runAllTests();
    return EXIT_SUCCESS;
}
