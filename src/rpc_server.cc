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

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "rpc_server.h"
#include "log.h"

static inline int _rpc_write(int sockfd, const void *buf, size_t bufsz) {
    struct iovec iov = {(void *) buf, bufsz};
    return ipc_channel_write(sockfd, iov);
}

static inline ssize_t _rpc_read(int sockfd, void *buf, size_t bufsz) {
    struct iovec iov = {(void *) buf, bufsz};
    return ipc_channel_read(iov, sockfd);
}

static void _rpc_op_list(int sockfd) {
    static const char *res = "hello world\n";
    if (_rpc_write(sockfd, res, strlen(res) + 1)) {
        // log error
    }
}

static void _rpc_op_version(int sockfd) {
    static const char *res = "relaunchd version unknown"; // FIXME
    if (_rpc_write(sockfd, res, strlen(res) + 1)) {
        // log error
    }
}

int rpc_dispatch(struct ipc_channel *chan) {
    int sockfd = ipc_channel_accept(chan);
    if (sockfd < 0) {
        log_error("accept failed");
        return -1;
    }

    char buf[IPC_MAX_MSGLEN];
    ssize_t bytes = _rpc_read(sockfd, buf, sizeof(buf));
    if (bytes <= 3) {
        log_error("got %zd", bytes);
        close(sockfd);
        return -1;
    }

    // TODO: parse this as a JSON array
    auto cmd = std::string{buf};

    // TODO: implement these ops:
//    enum rpc_op {
//        RPC_OP_LIST,
//        RPC_OP_LOAD,
//        RPC_OP_UNLOAD,
//        RPC_OP_ENABLE,
//        RPC_OP_DISABLE,
//        RPC_OP_KILL,
//        RPC_OP_PRINT,
//        RPC_OP_VERSION,
//    };

    try {
        if (cmd == "list") {
            _rpc_op_list(sockfd);
//    } else if (!strcmp(cmd, "load")) {
//        _rpc_op_load(sockfd, args);
        } else if (cmd == "version") {
            _rpc_op_version(sockfd);
        } else {
            log_error("unknown op");
        }
    } catch (std::exception &ex) {
        // log error
        (void) close(sockfd);
        return -1;
    }
    if (close(sockfd)) {
        log_error("socket(2): %s", strerror(errno));
        return -1;
    }
    return 0;
}

