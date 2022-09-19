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

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/event.h>

#include <string>
#include <system_error>
#include <sys/ioctl.h>

#include "log.h"
#include "memory.h"
#include "channel.h"

//#ifdef __cplusplus


struct ipc_channel ipc_channel_create() {
    struct ipc_channel chan;
    memset(&chan, 0, sizeof(chan));
    chan.sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (chan.sockfd < 0) {
        log_error("socket(2): %s", strerror(errno));
        goto err_out;
    }
    if (fcntl(chan.sockfd, F_SETFD, FD_CLOEXEC) < 0) {
        log_error("fcntl(2): %s", strerror(errno));
        goto err_out;
    }
    chan.addr.sun_family = AF_UNIX;
    return chan;
    err_out:
    chan.error = errno;
    return chan;
}

int ipc_channel_bind(struct ipc_channel *chan, const char *path) {
    if (chan->path) {
        log_error("already bound to path");
        return -1;
    }
    chan->path = strdup(path);
    if (!chan->path) {
        log_error("strdup(3): %s", strerror(errno));
        return -1;
    }

    strncpy((char *) &chan->addr.sun_path, path, sizeof(chan->addr.sun_path) - 1); // fixme check error

    int rv = bind(chan->sockfd, (struct sockaddr *) &chan->addr, sizeof(chan->addr));
    if (rv < 0) {
        if (errno == EADDRINUSE) {
            // FIXME: detect another process using it
            unlink(chan->path);
            if (bind(chan->sockfd, (struct sockaddr *) &chan->addr, sizeof(chan->addr)) < 0) {
                log_error("bind(2) to %s: %s", chan->path, strerror(errno));
                return -1;
            }
        } else {
            log_error("bind(2) to %s: %s", chan->path, strerror(errno));
            return -1;
        }
    }
    log_debug("bound to %s", chan->path);

    // TODO: add setsockopt(nonblocking) and handle the EWOULDBLOCK in the dispatch()

    return 0;
}

int ipc_channel_notify(struct ipc_channel *chan, int kqfd, void (*cb)(void *)) {
    struct kevent kev;
    EV_SET(&kev, chan->sockfd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, reinterpret_cast<void *>(cb));
    if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0) {
        log_error("kevent(2): %s", strerror(errno));
        return -1;
    }
    return 0;
}

ssize_t ipc_channel_read(struct iovec msg, int sockfd) {
    ssize_t bytes = read(sockfd, msg.iov_base, msg.iov_len);
    if (bytes < 0) {
        log_error("read(2): %s", strerror(errno));
        return -1;
    }
    log_debug("read %zu bytes from IPC channel", (size_t) bytes);
    return bytes;
}

int ipc_channel_write(int sockfd, struct iovec msg) {
    ssize_t bytes;
    bytes = write(sockfd, msg.iov_base, msg.iov_len);
    if (bytes < 0) {
        log_error("write(2): %s", strerror(errno));
        return -1;
    } else if ((size_t) bytes < msg.iov_len) {
        log_error("TODO - handle short write");
        return -1;
    }
    log_debug("wrote %ld bytes to IPC channel", bytes);
    return 0;
}

void ipc_channel_close(struct ipc_channel *chan) {
    if (chan->path) {
        free(chan->path);
    }
    if (chan->sockfd >= 0) {
        close(chan->sockfd);
    }
}

int ipc_channel_listen(struct ipc_channel *chan, int backlog) {
    if (chan->error || chan->sockfd < 0) {
        // log...
        return -1;
    }
    if (listen(chan->sockfd, backlog)) {
        log_error("listen(2): %s", strerror(errno));
        return -1;
    }
    return 0;
}

int ipc_channel_accept(struct ipc_channel *chan) {
    struct sockaddr_un saun;
    socklen_t len = sizeof(saun);
    int result = accept(chan->sockfd, (struct sockaddr *) &saun, &len);
    if (result < 0) {
        log_error("accept(2): %s", strerror(errno));
    }
    // TODO: setsockopt to make nonblocking, set buffer size
    // TODO: fcntl to set o_cloexec
    return result;
}

int ipc_channel_connect(struct ipc_channel *chan, const char *path) {
    if (chan->error || chan->sockfd < 0) {
        // log...
        return -1;
    }
    if (chan->path) {
        log_error("already bound to path");
        return -1;
    }

    chan->path = strdup(path);
    if (!chan->path) {
        log_error("strdup(3): %s", strerror(errno));
        return -1;
    }

    strncpy((char *) &chan->addr.sun_path, path, sizeof(chan->addr.sun_path) - 1); // fixme check error

    if (connect(chan->sockfd, (struct sockaddr *) &chan->addr, sizeof(chan->addr))) {
        log_error("connect(2): %s", strerror(errno));
        return -1;
    }

    return 0;
}

Channel::Channel() {
    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0) {
        log_error("socket(2): %s", strerror(errno));
        throw std::system_error(errno, std::system_category(), "socket(2) failed");
    }
    if (fcntl(sockfd, F_SETFD, FD_CLOEXEC) < 0) {
        log_error("fcntl(2): %s", strerror(errno));
        throw std::system_error(errno, std::system_category(), "fcntl(2) failed");
    }
    addr.sun_family = AF_UNIX;
}

void Channel::bindAndListen(const std::string &path, int backlog) {
    strncpy((char *) &addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    // FIXME check error

    int rv = bind(sockfd, (struct sockaddr *) &addr, sizeof(addr));
    if (rv < 0) {
        if (errno == EADDRINUSE) {
            // FIXME: detect another process using it
            unlink(path.c_str());
            if (bind(sockfd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
                log_error("bind(2) to %s: %s", path.c_str(), strerror(errno));
                throw std::system_error(errno, std::system_category(), "bind(2) failed");
            }
        } else {
            log_error("bind(2) to %s: %s", path.c_str(), strerror(errno));
            throw std::system_error(errno, std::system_category(), "bind(2) failed");
        }
    }
    log_debug("bound to %s", path.c_str());


    // TODO: add setsockopt(nonblocking) and handle the EWOULDBLOCK in the dispatch()

    if (listen(sockfd, backlog)) {
        log_error("listen(2): %s", strerror(errno));
        throw std::system_error(errno, std::system_category(), "listen(2) failed");
    }
}

void Channel::accept() {
    if (peerfd >= 0) {
        throw std::logic_error("already accepted a connection");
    }
    struct sockaddr_un saun;
    socklen_t len = sizeof(saun);
    int result = ::accept(sockfd, (struct sockaddr *) &saun, &len);
    if (result < 0) {
        log_error("accept(2): %s", strerror(errno));
        // throw?
        return; // false ?
    }
    // TODO: setsockopt to make nonblocking, set buffer size
    // TODO: fcntl to set o_cloexec
    peerfd = result;
}

int Channel::connect(const std::string &path) {
    strncpy((char *) &addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    // fixme check error

    if (::connect(sockfd, (struct sockaddr *) &addr, sizeof(addr))) {
        log_error("connect(2): %s", strerror(errno));
        return -1;
    }

    return 0;
}

json Channel::readMessage() {
    if (peerfd < 0 && sockfd < 0) {
        throw std::logic_error("must call accept() or connect() first");
    }
    int sd = (peerfd >= 0) ? peerfd : sockfd;
    char buf[IPC_MAX_MSGLEN];
    ssize_t bytes = read(sd, (char *) &buf, sizeof(buf));
    if (bytes < 0) {
        log_error("read(2): %s", strerror(errno));
        throw std::system_error(errno, std::system_category(), "read(2) failed");
    }
    log_debug("read %zu bytes from IPC channel", (size_t) bytes);
    try {
        return json::parse(buf);
    } catch (...) {
        log_error("json::parse() failed");
        throw std::runtime_error("JSON parse failed");
    }
}


void Channel::writeMessage(const json &j) {
    if (peerfd < 0 && sockfd < 0) {
        throw std::logic_error("must call accept() or connect() first");
    }
    int sd = (peerfd >= 0) ? peerfd : sockfd;
    std::string buf = j.dump();
    size_t bufsz = buf.length() + 1;
    ssize_t bytes = write(sd, buf.data(), bufsz);
    if ((size_t) bytes < bufsz) {
        log_error("write(2) of %zd bytes: %s", bytes, strerror(errno));
        throw std::system_error(errno, std::system_category(), "write(2) failed");
    }
    log_debug("wrote %ld bytes to IPC channel", bytes);
}


Channel::~Channel() {
    if (sockfd >= 0) {
        close(sockfd);
    }
    if (peerfd >= 0) {
        close(peerfd);
    }
}

void Channel::addEvent(int kqfd, void (*cb)(void *)) {
    struct kevent kev;
    EV_SET(&kev, sockfd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, reinterpret_cast<void *>(cb));
    if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0) {
        log_error("kevent(2): %s", strerror(errno));
        throw std::system_error(errno, std::system_category(), "kevent(2) failed");
    }
}

void Channel::disconnect() noexcept {
    if (peerfd >= 0) {
        if (close(peerfd)) {
            log_error("connect(2): %s", strerror(errno));
        }
        peerfd = -1;
    }
}
