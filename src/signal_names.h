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

#pragma once

#include <unordered_map>
#include <string>
#include <csignal>

namespace {
    inline const std::unordered_map<std::string, int> signals_by_name = {
#define X(x) { "SIG" #x , SIG ##x }
            // Standard signals defined by POSIX
            X(ABRT), X(ALRM), X(BUS), X(CHLD), X(CONT), X(FPE), X(HUP), X(ILL),
            X(INT), X(KILL), X(PIPE), X(PROF), X(QUIT), X(SEGV), X(STOP),
            X(TSTP), X(SYS), X(TERM), X(TRAP), X(TTIN), X(TTOU), X(URG), X(USR1),
            X(USR2), X(VTALRM), X(XCPU), X(XFSZ),
#ifdef SIGPOLL
            X(POLL),
#endif
#if __linux__
            // Non-standard signals supported by Linux
            X(IO), X(IOT), X(STKFLT), X(WINCH),
#endif // __linux__
#undef X
    };
}

//! Convert a symbolic signal name to a signal number.
//!
//! If the string is a valid number, it is assumed to be the
//! signal number. Otherwise, a case-insensitive search will
//! be performed. The leading "SIG" may be omitted so that
//! "TERM" will match the signal number for "SIGTERM".
//!
//! \param signum_or_name a string that refers to a signal
//! \return the signal number
std::optional<int>
getSignalByName(const std::string &signum_or_name) {
    int signum = -1;
    try {
        signum = std::stoi(signum_or_name);
    } catch (...) {
        auto buf = signum_or_name;
        for (auto &c: buf) c = (char) toupper(c);
        if (buf.find("SIG") != 0) {
            buf = "SIG" + buf;
        }
        auto it = signals_by_name.find(buf);
        if (it != signals_by_name.end()) {
            signum = it->second;
        }
    }
    if (signum >= 0) {
        return signum;
    } else {
        return std::nullopt;
    }
}
