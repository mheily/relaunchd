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
#include <iostream>

#include "channel.h"
#include "options.h"
#include "rpc_client.h"

void printUsage() {
    std::cout << "usage: ...\n";
}

int main(int argc, char *argv[]) {
    if (argc <= 1) {
        printUsage();
        exit(1);
    }
    if (argc == 2 && std::string(argv[1]).rfind("help") != std::string::npos) {
        printUsage();
        exit(0);
    }

    char *ipcsocketpath = rpc_get_socketpath();
    struct ipc_channel chan = ipc_channel_create();
    if (chan.error) {
        errx(1, "chan_create");
    }
    if (ipc_channel_connect(&chan, ipcsocketpath)) {
        errx(1, "connect");
    }

    auto subcommand = std::string(argv[1]);
    if (subcommand == "list") {
        std::cout << rpc_list(&chan) << std::endl;
    } else if (subcommand == "load") {
        for (int i = 2; i < argc; i++) {
            std::cout << argv[i] << "\n";
        }
    } else if (subcommand == "version") {
        std::cout << rpc_version(&chan) << std::endl;
    } else {
        std::cout << "ERROR: Unsupported subcommand\n";
        ipc_channel_close(&chan);
        exit(2);
    }
    free(ipcsocketpath);
    exit(0);
}
