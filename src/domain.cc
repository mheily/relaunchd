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

#include <array>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

#include "config.h"
#include "domain.h"

const std::string &Domain::to_string() const {
    switch (dtype) {
    case DomainType::System: {
        static const std::string str{"system"};
        return str;
    }
    case DomainType::User: {
        static const std::string str{"user"};
        return str;
    }
    case DomainType::GUI: {
        static const std::string str{"gui"};
        return str;
    }
    }
}

const std::vector<std::string> &Domain::getLoadPaths() const {
    switch (dtype) {
    case DomainType::System: {
        static const std::vector<std::string> result = {
            SYSTEM_DAEMON_LOAD_PATH,
            VENDOR_DAEMON_LOAD_PATH,
        };
        return result;
    }
    case DomainType::User: {
        static const std::vector<std::string> result = {
            SYSTEM_AGENT_LOAD_PATH,
            VENDOR_AGENT_LOAD_PATH,
        };
        return result;
    }
    case DomainType::GUI: {
        static const std::vector<std::string> result = {
            USER_AGENT_LOAD_PATH,
        };
        return result;
    }
    }
}

DomainType Domain::detectDomainType() {
    if (getuid() == 0) {
        return DomainType::System;
    } else {
        // XXX-FIXME probably need to implement "launchctl bootstrap"
        // to initialize these correctly.
        if (getenv("DISPLAY")) {
            return DomainType::GUI;
        } else {
            return DomainType::User;
        }
    }
}

std::filesystem::path Domain::detectStateDir() {
    if (dtype == DomainType::System) {
        return PKGSTATEDIR;
    }

    //
    // User and GUI share the same state directory.
    //

    // The subdirectory under $HOME where state for the user domains are
    // stored Can be overridden by setting the $XDG_STATE_HOME variable
    std::filesystem::path state_home;
    const char *xdg_state_home = getenv("XDG_STATE_HOME");
    if (xdg_state_home) {
        state_home = std::filesystem::path{xdg_state_home};
    } else {
        char *home_p = getenv("HOME");
        if (home_p) {
            state_home = std::filesystem::path{home_p} / ".local" / "state";
        }
    }
#if 0
    // FIXME this needs work

} else {
    // This is useful inside a container where HOME is not set..
    // XXX FIXME: must be only writable by the user
    char *tmpdir_p = getenv("TMPDIR");
    state_home = tmpdir_p ? std::string{tmpdir_p}
                          : std::string{"/tmp/relaunchd-" +
                                        std::to_string(getuid())};
}
#endif

    if (state_home.empty()) {
        throw std::runtime_error("unable to determine the state directory");
    }
    return state_home / "relaunchd";
}

Domain::Domain(DomainType t, std::filesystem::path statedir_)
    : dtype(t), statedir(std::move(statedir_)) {
    // FIXME: validate permissions of statedir
}
