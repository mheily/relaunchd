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

/*
 * A "channel" is a bidirectional AF_LOCAL socket used for
 * inter-process communication.
 */

#pragma once

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>

/* Maximum length of an IPC message */
#define IPC_MAX_MSGLEN  32768U

#include <string>
#include "../vendor/json.hpp"
using json = nlohmann::json;

class Channel {
public:
    Channel();
    ~Channel();
    void bindAndListen(const std::string &path, int backlog);
    void accept();
    int connect(const std::string &path);
    void disconnect() noexcept;
    json readMessage();
    void writeMessage(const json &j);
    int getSockFD();

private:
    struct sockaddr_un addr;
    int sockfd = -1;
    int peerfd = -1; // the other side of the channel
};
