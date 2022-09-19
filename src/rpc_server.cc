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

#include <stdexcept>
#include <string>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "rpc_server.h"
#include "log.h"


//static void _rpc_op_list(int sockfd) {
//    static const char *res = "hello world\n";
//    if (_rpc_write(sockfd, res, strlen(res) + 1)) {
//        // log error
//    }
//}

static void _rpc_op_version(Channel &chan, const json &) {
    chan.writeMessage("relaunchd version unknown"); // FIXME get version number
}

// FIXME: needs a lot more error checking
int rpc_dispatch(Channel &chan) {
    static const std::unordered_map<std::string, void(*)(Channel &chan, const json &j)> handlers = {
            {"version", _rpc_op_version},
    };
    chan.accept();
    auto msg = chan.readMessage();
    try {
        auto method = msg.at(0).get<std::string>();
        auto funcptr = handlers.at(method);
        (*funcptr)(chan, msg);
    } catch (...) {
        log_error("dispatch failed");
    }

    chan.disconnect();

    return 0;
}

