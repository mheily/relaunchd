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
#include "state_file.hpp"

using namespace std;

void testStateFile() {
    const std::string statefilepath = tmpdir + "/test_statefile.json";
    if (std::filesystem::exists(statefilepath)) {
        std::filesystem::remove(statefilepath);
    }
    json default_value = {"test", 1};
    auto sf = StateFile(statefilepath, default_value);
    json buf = sf.getValue();
    std::filesystem::remove(statefilepath);
    assert(buf == default_value);
}

void testSetValue() {
    const std::string statefilepath = tmpdir + "/test_setValue.json";
    if (std::filesystem::exists(statefilepath)) {
        std::filesystem::remove(statefilepath);
    }
    json value = "test";
    auto sf = StateFile(statefilepath, value);
    value = "test2";
    sf.setValue(value);
    json buf = sf.getValue();
    std::filesystem::remove(statefilepath);
    assert(buf == value);
}

void addStateFileTests(TestRunner &runner) {
    runner.addTest("testStateFile", testStateFile);
    runner.addTest("testSetValue", testSetValue);
}
