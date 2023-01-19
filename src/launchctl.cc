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
#include <filesystem>
#include <iostream>

#include "channel.h"
#include "domain.h"
#include "options.h"
#include "rpc_client.h"

void printUsage() { std::cout << "usage: ...\n"; }

int launchctl_main(int argc, char *argv[]) {
    if (argc <= 1) {
        printUsage();
        return EXIT_FAILURE;
    }
    if (argc == 2 && std::string(argv[1]).rfind("help") != std::string::npos) {
        printUsage();
        return EXIT_SUCCESS;
    }

    RpcClient client;

    std::vector<std::string> args(argv + 2, argv + argc);
    auto subcommand = std::string(argv[1]);
    if (!client.methodExists(subcommand)) {
        std::cerr << "ERROR: unknown subcommand" << std::endl;
        return EXIT_FAILURE;
    }
    try {
        client.invokeMethod(subcommand, args, Domain());
    } catch (const std::exception &exc) {
        std::cerr << "ERROR: Unhandled exception: " << exc.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
