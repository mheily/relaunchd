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

#include <system_error>
#include <string>
#include <unistd.h>

#include "log.h"

enum class ExecErrorCode {
    //! There were no errors
    ExecSuccess = 0,
    //! setsid(2) failed
    CreateSessionFailed,
    //! chdir(2) to the WorkingDirectory failed
    SetWorkingDirectoryFailed,
    //! chroot(2) to the RootDirectory failed
    SetRootDirectoryFailed,
    //! setpriority(2) failed
    SetPriorityFailed,
    //! initgroups(3) failed
    InitGroupsFailed,
    //! setgid(2) failed
    SetGroupIdFailed,
    //! setlogin(2) failed
    SetLoginFailed,
    //! setuid(2) failed
    SetUserIdFailed,
    //! open(2) failed
    OpenFailed,
    //! read(2) failed
    ReadFailed,
    //! close(2) failed
    CloseFailed,
    //! dup2(2) failed
    Dup2Failed,
    //! Failed malloc() call or similar
    MemoryAllocationFailed,
    //! Failed execve() call or similar
    ExecFailed,
    //! The post-fork cleanup handler failed
    ForkHandlerFailed,
};

//! An error message passed from the child process to the parent prior to exec()
struct ExecStatus {
    ExecErrorCode errorCode;
    int savedErrno = 0;
    enum {
        ChildProcess = 0,
        RedirectStdin = 1,
        RedirectStdout = 2,
        RedirectStderr = 3,
        ParentProcess = 4,
    } errorContext = ChildProcess;

    [[nodiscard]] std::string getErrorCode() const {
        switch (errorCode) {
        case ExecErrorCode::ExecSuccess:
            return "ExecSuccess";
        case ExecErrorCode::CreateSessionFailed:
            return "CreateSessionFailed";
        case ExecErrorCode::SetWorkingDirectoryFailed:
            return "SetWorkingDirectoryFailed";
        case ExecErrorCode::SetRootDirectoryFailed:
            return "SetRootDirectoryFailed";
        case ExecErrorCode::SetPriorityFailed:
            return "SetPriorityFailed";
        case ExecErrorCode::InitGroupsFailed:
            return "InitGroupsFailed";
        case ExecErrorCode::SetGroupIdFailed:
            return "SetGroupIdFailed";
        case ExecErrorCode::SetLoginFailed:
            return "SetLoginFailed";
        case ExecErrorCode::SetUserIdFailed:
            return "SetUserIdFailed";
        case ExecErrorCode::OpenFailed:
            return "OpenFailed";
        case ExecErrorCode::ReadFailed:
            return "ReadFailed";
        case ExecErrorCode::CloseFailed:
            return "CloseFailed";
        case ExecErrorCode::Dup2Failed:
            return "Dup2Failed";
        case ExecErrorCode::MemoryAllocationFailed:
            return "MemoryAllocationFailed";
        case ExecErrorCode::ExecFailed:
            return "ExecFailed";
        case ExecErrorCode::ForkHandlerFailed:
            return "ForkHandlerFailed";
        default:
            throw std::runtime_error("Invalid error code");
        }
    }

    //! Convert the status to a string
    [[nodiscard]] std::string toString() const {
        using namespace std;
        auto s = string{"[ExecStatus: code="} + getErrorCode() +
                 " context=" + to_string(errorContext);
        if (savedErrno) {
            s += " errno=" + string{strerror(savedErrno)} + "(" +
                 to_string(savedErrno) + ")";
        }
        s += "]";
        return s;
    }
};

//! Monitor what happens in a child process between fork() and exec()
class ExecMonitor {
  public:
    ExecMonitor() {
        if (pipe(pfd) != 0) {
            log_errno("pipe(2)");
            throw std::system_error(errno, std::system_category(), "fork(2)");
        }
        for (int fd : pfd) {
            if (fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) {
                log_errno("fcntl(2)");
                int saved_errno = errno;
                (void)close(pfd[0]);
                (void)close(pfd[1]);
                throw std::system_error(saved_errno, std::system_category(),
                                        "fcntl(2)");
            }
        }
    }

    ~ExecMonitor() {
        for (int fd : pfd) {
            (void)close(fd);
        }
    }

    void becomeChild() {
        (void)close(pfd[0]);
        pfd[0] = -1;
    }

    void becomeParent() {
        (void)close(pfd[1]);
        pfd[1] = -1;
    }

    //! Get the read side of the pipe, to allow for poll/kqueue monitoring
    [[nodiscard]] int getReaderFd() const {
        if (pfd[0] < 0) {
            throw std::logic_error("missing descriptor");
        }
        return pfd[0];
    }

    ExecStatus readStatus() {
        ExecStatus result;
        ssize_t bytes = read(pfd[0], &result, sizeof(result));
        if (bytes < 0) {
            return ExecStatus{ExecErrorCode::ReadFailed, errno,
                              ExecStatus::ParentProcess};
        } else if (bytes == 0) {
            return ExecStatus{ExecErrorCode::ExecSuccess, 0,
                              ExecStatus::ParentProcess};
        } else if (bytes < (long)sizeof(result)) {
            log_error("short read from pipe");
            return ExecStatus{ExecErrorCode::ReadFailed, 0,
                              ExecStatus::ParentProcess};
        } else {
            return result;
        }
    }

    void writeStatus(ExecStatus status) {
        if (write(pfd[1], &status, sizeof(status)) != sizeof(status)) {
            // This will likely go to /dev/null, perhaps send it elsewhere?
            log_error("Error writing to pipe");
        }
    }

  private:
    int pfd[2];
};
