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
#include <sys/socket.h>

#include <string>
#include <system_error>
#include <sys/ioctl.h>

#include "log.h"
#include "memory.h"
#include "channel.h"


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

    // DEADWOOD: Linux has no SO_NOSIGPIPE, so we SIG_IGN the SIGPIPE signal instead.
//    int set = 1;
//    if (setsockopt(result, SOL_SOCKET, SO_NOSIGPIPE, (void *)&set, sizeof(set))) {
//        log_error("setsockopt(2): %s", strerror(errno));
//        close(result);
//        return; // false?
//    }

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
