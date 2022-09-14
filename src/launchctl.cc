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

#include <iostream>

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
    auto subcommand = std::string(argv[1]);
    if (subcommand == "load") {
        for (int i = 2; i < argc; i++) {
            std::cout << argv[i] << "\n";
        }
    } else {
        std::cout << "ERROR: Unsupported subcommand\n";
        exit(2);
    }
}
