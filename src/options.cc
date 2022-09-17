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

#include <string>
#include <filesystem>

#include <string.h> // for strdup

#include "options.h"

struct launchd_options options;

char * rpc_get_socketpath() try {
    std::string statedir;
    const char *xdg_state_home = getenv("XDG_STATE_HOME");
    if (xdg_state_home) {
        statedir = std::string{xdg_state_home};
    } else {
        statedir = std::string{getenv("HOME")} + "/.local/state";
    }
    statedir += "/relaunchd";
    std::filesystem::create_directories(statedir);
    std::string socketpath = statedir + "/rpc.sock";
    return strdup(socketpath.c_str());
} catch(...) {
    // log error
    return nullptr;
}
