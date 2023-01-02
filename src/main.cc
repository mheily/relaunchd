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
#include <string>

extern int launchd_main(int argc, char *argv[]);

extern int launchctl_main(int argc, char *argv[]);

extern int test_main(int argc, char *argv[]);

#ifndef RELAUNCHD_UNIT_TESTS
static bool ends_with(const std::string &path, const std::string &tail) {
    if (path.length() >= tail.length()) {
        return !path.compare(path.length() - tail.length(), tail.length(),
                             tail);
    } else {
        return false;
    }
}
#endif

int main(int argc, char *argv[]) {
    if (!argc) {
        exit(127);
    }
#ifdef RELAUNCHD_UNIT_TESTS
    return test_main(argc, argv);
#else
    const std::string program_path{argv[0]};
    if (ends_with(program_path, "launchd")) {
        return launchd_main(argc, argv);
    } else if (ends_with(program_path, "launchctl")) {
        return launchctl_main(argc, argv);
    } else {
        std::cerr << "ERROR: unhandled value of argv[0]" << std::endl;
        exit(128);
    }
#endif
}
