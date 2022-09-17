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

#include <string.h>
#include "rpc_client.h"

// TODO -- make a _rpc_printf() and _rpc_scanf() ? or just use JSON
//-- TODO -- deduplicate these helper funcs, they are in both server+client

static inline int _rpc_write(int sockfd, const void * buf, size_t bufsz) {
    struct iovec iov = {(void*) buf, bufsz};
    return ipc_channel_write(sockfd, iov);
}

static inline ssize_t _rpc_read(int sockfd, void *buf, size_t bufsz) {
    struct iovec iov = {(void*) buf, bufsz};
    return ipc_channel_read(iov, sockfd);
}

//---

std::string rpc_list(struct ipc_channel *chan) {
    static const char *msg = "list";
    if (_rpc_write(chan->sockfd, msg, strlen(msg) + 1)) {
        throw std::runtime_error("write failed");
    }

    char buf[IPC_MAX_MSGLEN];
    ssize_t bytes = _rpc_read(chan->sockfd, buf, sizeof(buf));
    if (bytes < 0) {
        throw std::runtime_error("read failed");
    }
    return std::string{buf, (size_t)bytes};
}


std::string rpc_version(struct ipc_channel *chan) {
    static const char *msg = "version";
    if (_rpc_write(chan->sockfd, msg, strlen(msg) + 1)) {
        throw std::runtime_error("write failed");
    }
    char buf[IPC_MAX_MSGLEN];
    ssize_t bytes = _rpc_read(chan->sockfd, buf, sizeof(buf));
    if (bytes < 1) {
        throw std::runtime_error("read failed");
    }
    return std::string{buf, (size_t)bytes - 1};
}
