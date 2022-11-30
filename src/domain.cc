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
#include <vector>

#include "config.h"
#include "domain.h"

const std::string &Domain::to_string() const {
    static const std::array<std::string, 3> names = {"system", "user", "gui"};
    return names.at(dtype);
}

const std::vector<std::string> &Domain::getLoadPaths() const {
    switch (dtype) {
    case DOMAIN_TYPE_SYSTEM: {
        static const std::vector<std::string> result = {
            SYSTEM_DAEMON_LOAD_PATH,
            VENDOR_DAEMON_LOAD_PATH,
        };
        return result;
    }
    case DOMAIN_TYPE_USER: {
        static const std::vector<std::string> result = {
            SYSTEM_AGENT_LOAD_PATH,
            VENDOR_AGENT_LOAD_PATH,
        };
        return result;
    }
    case DOMAIN_TYPE_GUI: {
        static const std::vector<std::string> result = {
            USER_AGENT_LOAD_PATH,
        };
        return result;
    }
    default:
        throw std::range_error("invalid dtype");
    }
}
