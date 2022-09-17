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
 *
 * TODO: once this is pure C++, close the socket via the destructor
 *    and make this a class with methods.
 */

#pragma once

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>

/* Maximum length of an IPC message */
#define IPC_MAX_MSGLEN  32768U

// needed by manager.c
#ifdef __cplusplus
extern "C" {
#endif

struct ipc_channel {
    char *path;
    struct sockaddr_un addr;
    int sockfd;
    int error;
};

struct ipc_channel ipc_channel_create();
void ipc_channel_close(struct ipc_channel *chan);
int ipc_channel_bind(struct ipc_channel *chan, const char *path);
int ipc_channel_listen(struct ipc_channel *chan, int backlog);
int ipc_channel_connect(struct ipc_channel *chan, const char *path);
int ipc_channel_accept(struct ipc_channel *chan);
int ipc_channel_notify(struct ipc_channel *chan, int kqfd, void (*cb)(void *));
ssize_t ipc_channel_read(struct iovec msg, int sockfd);
int ipc_channel_write(int sockfd, struct iovec msg);

#ifdef __cplusplus
}
#endif
