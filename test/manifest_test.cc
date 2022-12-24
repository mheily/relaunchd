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

#include <filesystem>
#include <iostream>

#include "common.hpp"
#include "manifest.h"
#include "log.h"

using namespace std;

const filesystem::path manifestdir{string{TESTDIR} + "/fixtures"};

void test_parse() {
    using filesystem::directory_iterator;
    for (const auto &file: directory_iterator(manifestdir)) {
        try {
            auto jsondata = manifest::parse(file.path());
            Manifest manifest = jsondata.get<Manifest>();
        } catch (...) {
            cerr << "failed to parse: " << file.path() << endl;
            throw;
        }
    }
}

void testParseComplexDependency() {
    json obj = json::parse(R"(
        [
            {
                "Label": "dep1",
                "StartBefore": true
            }
        ]
    )");
    DependencyList deplist{obj};
}

void testParseSimpleDependency() {
    json obj = json::parse(R"(
        [
            "dep1",
            "dep2"
        ]
    )");
    DependencyList deplist{obj};
}

void testParseUmaskFromStr() {
    json manifest = json{
            {"Label", "testParseUmaskFromStr"},
            {"Program", "/bin/cat"},
            {"Umask", "0755"}
    };
    Manifest m;
    manifest::from_json(manifest, m);
    assert(m.umask.value() == 493);
}

void testParseUmaskFromInt() {
    json manifest = json{
            {"Label", "testParseUmaskFromStr"},
            {"Program", "/bin/cat"},
            {"Umask", 493}
    };
    Manifest m;
    manifest::from_json(manifest, m);
    assert(m.umask.value() == 493);
}

void addManifestTests(TestRunner &runner) {
    runner.addTest("testParseUmaskFromStr", testParseUmaskFromStr);
    runner.addTest("testParseUmaskFromInt", testParseUmaskFromInt);
    runner.addTest("testParseComplexDependency", testParseComplexDependency);
    runner.addTest("testParseSimpleDependency", testParseSimpleDependency);
}
