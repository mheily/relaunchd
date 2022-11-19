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
 * A thin abstraction layer over the available kernel event API.
 */

#pragma once

#if __has_include(<sys/epoll.h>)
#define USE_EPOLL 1
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <optional>
#include <queue>
#elif __has_include(<sys/event.h>)
#define USE_KQUEUE 1
#include <sys/event.h>
#else
#error No supported kernel event API detected
#endif

#include <iostream>
#include <stdexcept>
#include <functional>
#include <unordered_set>
#include <variant>
#include <vector>

#include <csignal>
#include <fcntl.h>
#include <unistd.h>

// Set this to 1 to print debugging information to stderr
#ifndef KQLITE_TRACE
#define KQLITE_TRACE 0
#endif

namespace kqtrace {
    static inline void print(const std::string &msg) {
        if (KQLITE_TRACE) {
            std::cerr << "[EVENT] " << msg << std::endl;
        }
    }
}

struct proc_event {
    pid_t pid;
    int status;
};

struct signal_event {
    int signum;
};

struct socket_event {
    int sockfd;
};

struct timer_event {
    int timer_id;
};

// N.B. event_type must be kept in sync with the std::variant below.
typedef std::variant<proc_event, signal_event, socket_event, timer_event> Event;
enum event_type {
    EVTYPE_PROC,
    EVTYPE_SIGNAL,
    EVTYPE_SOCKET_READ,
    EVTYPE_TIMER,
};


class KernelEventInterface {
public:
    virtual ~KernelEventInterface() = default;

    virtual Event waitForEvent() = 0;

    virtual void monitorChildProcess(pid_t pid) = 0;

    virtual void ignoreChildProcess(pid_t pid) = 0;

    virtual void monitorSignal(int signum) = 0;

    virtual void unblockSignal(int signum) = 0;

    virtual void ignoreSignal(int signum) = 0;

    virtual void monitorSocketRead(int sockfd) = 0;

    virtual void ignoreSocketRead(int sockfd) = 0;

    virtual void monitorTimer(int timer_id, int milliseconds) = 0;

    virtual void ignoreTimer(int timer_id) = 0;

    //! Run cleanup actions between fork() and exec(), such as resetting signal handlers
    virtual void handleFork() = 0;
};

#if USE_EPOLL

class EpollImplementation : public KernelEventInterface {
public:
    EpollImplementation() {
        epfd = epollCreate();
        cleanup_fds.insert(epfd);

        sigemptyset(&sigmask);
        sigfd = signalfd(-1, &sigmask, SFD_NONBLOCK | SFD_CLOEXEC);
        if (sigfd < 0) {
            throw std::system_error(errno, std::system_category(), "signalfd()");
        }
        cleanup_fds.insert(sigfd);
        epollAdd(sigfd, EVTYPE_SIGNAL);
        monitorSignal(SIGCHLD);

        socket_read_fd = epollCreate();
        cleanup_fds.insert(socket_read_fd);
        epollAdd(socket_read_fd, EVTYPE_SOCKET_READ);

        timer_epfd = epollCreate();
        cleanup_fds.insert(timer_epfd);
        epollAdd(timer_epfd, EVTYPE_TIMER);
    }

    ~EpollImplementation() {
        for (auto &fd: cleanup_fds) {
            close(fd);
        }
        for (int signum : blocked_signals) {
            unblockSignal(signum);
        }
    }

    void handleFork() override {
        for (int signum : blocked_signals) {
            unblockSignal(signum);
        }
    }

    Event waitForEvent() override {
        if (!pending_events.empty()) {
            auto event = pending_events.front();
            pending_events.pop();
            return event;
        }
        for (;;) {
            int evtype;
            {
                auto maybe_event = epollGetOne(epfd);
                if (!maybe_event) {
                    continue;
                }
                const auto &event = *maybe_event;
                evtype = event.data.u32;
            }
            switch (evtype) {
                case EVTYPE_SIGNAL: {
                    getSignalEvents();
                    if (!pending_events.empty()) {
                        auto event = pending_events.front();
                        pending_events.pop();
                        return event;
                    }
                }
                    break;
                case EVTYPE_SOCKET_READ: {
                    auto event = epollGetOne(socket_read_fd);
                    if (event) {
                        return Event(socket_event{static_cast<int>(event->data.fd)});
                    }
                }
                    break;
                case EVTYPE_TIMER: {
                    auto event = epollGetOne(timer_epfd);
                    if (event) {
                        uint64_t expired;
                        const auto &fd = event->data.fd;
                        ssize_t n = read(fd, &expired, sizeof(expired));
                        if (n != sizeof(expired)) {
                            if (errno == EAGAIN) {
                                kqtrace::print("spurious timer wakeup");
                                break;
                            } else {
                                throw std::system_error(errno, std::system_category(), "read()");
                            }
                        }
                        (void) expired; // We don't return this to the caller, but should in the future.
                        for (const auto &[timer_id, tfd]: timerfd_map) {
                            if (tfd == fd) {
                                return Event(timer_event{timer_id});
                            }
                        }
                        throw std::range_error("timer FD does not match any known timer IDs");
                    }
                }
                    break;
                default:
                    throw std::range_error("invalid filter");
            }
        }
    }

    void monitorChildProcess(pid_t pid) override {
        // We could use pidfd_open(2) but this requires Linux 5.3+
        // For now, just use SIGCHLD and assume that all children will be reaped via SIGCHLD handling
        watch_pids.insert(pid);
    }

    void ignoreChildProcess(pid_t pid) override {
        watch_pids.erase(pid);
    }

    void monitorSocketRead(int sockfd) override {
        // TODO: use getsockopt() to verify this is a socket
        struct epoll_event epev;
        epev.events = EPOLLIN;
        epev.data.fd = sockfd;
        if (epoll_ctl(socket_read_fd, EPOLL_CTL_ADD, sockfd, &epev) < 0) {
            throw std::system_error(errno, std::system_category(), "epoll_ctl()");
        }
    }

    void ignoreSocketRead(int sockfd) override {
        struct epoll_event epev;
        epev.events = EPOLLIN;
        if (epoll_ctl(epfd, EPOLL_CTL_DEL, sockfd, &epev) < 0) {
            throw std::system_error(errno, std::system_category(), "epoll_ctl()");
        }
    }

    void monitorSignal(int signum) override {
        sigaddset(&sigmask, signum);
        if (signalfd(sigfd, &sigmask, 0) < 0) {
            throw std::system_error(errno, std::system_category(), "signalfd()");
        }
        changeSignalMask(SIG_BLOCK, signum);
        blocked_signals.insert(signum);
    }

    void unblockSignal(int signum) override {
        changeSignalMask(SIG_UNBLOCK, signum);
        blocked_signals.erase(signum);
    }

    void ignoreSignal(int signum) override {
        sigdelset(&sigmask, signum);
        if (signalfd(sigfd, &sigmask, 0) < 0) {
            throw std::system_error(errno, std::system_category(), "signalfd()");
        }
        unblockSignal(signum);
    }

    void monitorTimer(int timer_id, int milliseconds) override {
        int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        if (tfd < 0) {
            throw std::system_error(errno, std::system_category(), "signalfd()");
        }
        cleanup_fds.insert(tfd);
        // Convert milliseconds to seconds+milliseconds
        int tv_sec = milliseconds / 1000;
        int tv_nsec = (milliseconds % 1000) * 1000000;
        struct timespec ts = {tv_sec, tv_nsec};

        struct itimerspec its = {{0, 0}, ts};
        if (timerfd_settime(tfd, 0, &its, NULL) < 0) {
            throw std::system_error(errno, std::system_category(), "timerfd_settime()");
        }

        struct epoll_event epev;
        epev.events = EPOLLIN;
        epev.data.u32 = tfd;
        if (epoll_ctl(timer_epfd, EPOLL_CTL_ADD, tfd, &epev) < 0) {
            throw std::system_error(errno, std::system_category(), "timerfd_settime()");
        }
        timerfd_map.insert({{timer_id, tfd}});
    }

    void ignoreTimer(int timer_id) override {
        int tfd = timerfd_map.at(timer_id);
        int rv = epoll_ctl(epfd, EPOLL_CTL_DEL, tfd, nullptr);
        int saved_errno = errno;
        (void) close(tfd); // FIXME: err handling
        timerfd_map.erase(timer_id);
        cleanup_fds.erase(tfd);
        if (rv < 0) {
            errno = saved_errno;
            throw std::system_error(errno, std::system_category(), "epoll_ctl()");
        }
    }

private:
    void getSignalEvents() {
        bool sigchild_seen = false;
        for (;;) {
            struct signalfd_siginfo sig;
            ssize_t n = read(sigfd, &sig, sizeof(sig));
            if (n != sizeof(sig)) {
                if (errno == EWOULDBLOCK) {
                    break;
                } else {
                    throw std::system_error(errno, std::system_category(), "read()");
                }
            }
            if (sig.ssi_signo == SIGCHLD) {
                sigchild_seen = true;
            } else {
                pending_events.emplace(Event(signal_event{static_cast<int>(sig.ssi_signo)}));
            }
        }
        // Reap all zombies and create process events
        if (sigchild_seen) {
            for (;;) {
                int status;
                pid_t pid = waitpid(-1, &status, WNOHANG);
                if (pid == 0) {
                    break;
                } else if (pid < 0) {
                    if (errno == ECHILD) {
                        break;
                    } else {
                        throw std::system_error(errno, std::system_category(), "waitpid()");
                    }
                }
                if (watch_pids.count(pid)) {
                    pending_events.emplace(Event(proc_event{pid, status}));
                    watch_pids.erase(pid);
                } else {
                    // TODO maybe just return the event anyway and ignore it at a higher layer in the stack?
                    continue;
                }
            }
        }
    }

    static void changeSignalMask(int how, int signum) {
        sigset_t delta;
        sigemptyset(&delta);
        sigaddset(&delta, signum);
        if (sigprocmask(how, &delta, NULL) < 0) {
            throw std::system_error(errno, std::system_category(), "sigprocmask()");
        }
    }

    static int epollCreate() {
        int fd = epoll_create(10);
        if (fd < 0) {
            throw std::system_error(errno, std::system_category(), "epoll_create()");
        }
        if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
            close(fd);
            throw std::system_error(errno, std::system_category(), "fcntl()");
        }
        return fd;
    }

    void epollAdd(const int child_fd, enum event_type evtype) {
        struct epoll_event epev;
        epev.events = EPOLLIN;
        epev.data.u32 = evtype;
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, child_fd, &epev) < 0) {
            throw std::system_error(errno, std::system_category(), "epoll_ctl()");
        }
    }

    std::optional<epoll_event> epollGetOne(int fd) {
        struct epoll_event event;
        int rv = epoll_wait(fd, &event, 1, -1);
        if (rv > 0) {
            return event;
        } else if (rv == 0) {
            return std::nullopt;
        } else {
            throw std::system_error(errno, std::system_category(), "epoll_wait()");
        }
    }

    int epfd = -1;
    int sigfd = -1;
    int socket_read_fd = -1;
    int timer_epfd = -1;
    sigset_t sigmask;                   // signals to catch
    std::unordered_set<int> watch_pids; // process IDs to monitor
    std::unordered_map<int, int> timerfd_map;
    std::unordered_set<int> cleanup_fds; // file descriptors to close via the destructor
    std::unordered_set<int> blocked_signals; // signals that need to be unblocked when cleaning up
    std::queue<Event> pending_events;
};

#elif USE_KQUEUE

class KqueueImplementation : public KernelEventInterface {
public:
    KqueueImplementation() {
        kqfd = kqueue();
        if (kqfd < 0) {
            throw std::system_error(errno, std::system_category(), "kqueue()");
        }
        if (fcntl(kqfd, F_SETFD, FD_CLOEXEC) < 0) {
            close(kqfd);
            throw std::system_error(errno, std::system_category(), "fcntl()");
        }
    }

    ~KqueueImplementation() {
        close(kqfd);
    }

    Event waitForEvent() override {
        struct kevent kev;
        for (;;) {
            int rv = kevent(kqfd, NULL, 0, &kev, 1, NULL);
            if (rv > 0) {
                break;
            } else if (rv == 0) {
                continue;
            } else {
                if (errno == EINTR) {
                    kqtrace::print("kevent() was interrupted by a signal, will continue");
                    continue;
                }
                throw std::system_error(errno, std::system_category(), "kevent()");
            }
        }
        switch (kev.filter) {
            case EVFILT_PROC:
                return Event(proc_event{static_cast<pid_t>(kev.ident), static_cast<int>(kev.data)});
            case EVFILT_SIGNAL:
                return Event(signal_event{static_cast<int>(kev.ident)});
            case EVFILT_READ:
                return Event(socket_event{static_cast<int>(kev.ident)});
            case EVFILT_TIMER:
                return Event(timer_event{static_cast<int>(kev.ident)});
            default:
                throw std::range_error("invalid filter");
        }
    }

     void monitorChildProcess(pid_t pid) override {
         changeKevent(pid, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT);
    }

     void ignoreChildProcess(pid_t pid) override {
        try {
            changeKevent(pid, EVFILT_PROC, EV_DELETE, NOTE_EXIT);
        } catch (const std::system_error &e) {
            if (e.code().value() != ENOENT) {
                throw;
            }
        }
     }

    void monitorSocketRead(int sockfd) override {
        changeKevent(sockfd, EVFILT_READ, EV_ADD, NOTE_EXIT);
    }

    void ignoreSocketRead(int sockfd) override {
        changeKevent(sockfd, EVFILT_READ, EV_DELETE, NOTE_EXIT);
    }

    void monitorSignal(int signum) override {
        if (signum == SIGCHLD) {
            throw std::range_error("SIGCHLD is handled internally to support Linux");
        }
        if (signal(signum, SIG_IGN) == SIG_ERR) {
            throw std::system_error(errno, std::system_category(), "signal()");
        }
        changeKevent(signum, EVFILT_SIGNAL, EV_ADD, NOTE_EXIT);
    }

    void unblockSignal(int signum) override {
        if (signal(signum, SIG_DFL) == SIG_ERR) {
            throw std::system_error(errno, std::system_category(), "signal()");
        }
    }

    void ignoreSignal(int signum) override {
        unblockSignal(signum);
        changeKevent(signum, EVFILT_SIGNAL, EV_DELETE, NOTE_EXIT);
    }

    void monitorTimer(int timer_id, int milliseconds) override {
        changeKevent(timer_id, EVFILT_TIMER, EV_ADD | EV_ONESHOT, 0, milliseconds);
    }

    void ignoreTimer(int timer_id) override {
        changeKevent(timer_id, EVFILT_TIMER, EV_DELETE, 0);
    }

    void handleFork() override {}

private:
    void changeKevent(uintptr_t ident, int filter, int flags, int fflags, intptr_t data = 0) {
        struct kevent kev;
        EV_SET(&kev, ident, filter, flags, fflags, data, NULL);
        if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0) {
            throw std::system_error(errno, std::system_category(), "kevent()");
        }
    }
    int kqfd = -1;
};

#endif  // USE_KQUEUE

namespace kq {
    class EventManager {
    public:
        EventManager() {
#if USE_EPOLL
            impl = std::make_unique<EpollImplementation>();
#elif USE_KQUEUE
            impl = std::make_unique<KqueueImplementation>();
#else
#error Not supported
#endif
        }

        void addSignal(int signum, std::function<void(int)> callback) {
            impl->monitorSignal(signum);
            signal_callbacks.insert({{signum, callback}});
        }

        void addProcess(pid_t pid, std::function<void(pid_t, int)> callback) {
            impl->monitorChildProcess(pid);
            process_callbacks.insert({{pid, callback}});
        }

        void deleteProcess(pid_t pid) {
            impl->ignoreChildProcess(pid);
            process_callbacks.erase(pid);
        }

        void deleteTimer(int timer_id) {
            impl->ignoreTimer(timer_id);
            timer_callbacks.erase(timer_id);
        }

        void addSocketRead(int sd, std::function<void(int)> callback) {
            impl->monitorSocketRead(sd);
            socket_read_callbacks.insert({{sd, callback}});
        }

        int addTimer(int seconds, std::function<void()> callback) {
            int timer_id = getNextTimerId();
            auto milliseconds = seconds * 1000;
            kqtrace::print("adding timer for " + std::to_string(milliseconds) + "ms");
            impl->monitorTimer(timer_id, milliseconds);
            timer_callbacks.insert({{timer_id, callback}});
            return timer_id;
        }

        void waitForEvent() {
            auto event = impl->waitForEvent();
            kqtrace::print("got an event of type " + std::to_string(event.index()));
            switch (event.index()) {
                case EVTYPE_SIGNAL: {
                    const auto &signum = std::get<signal_event>(event).signum;
                    const auto &callback = signal_callbacks.at(signum);
                    callback(signum);
                    break;
                }
                case EVTYPE_PROC: {
                    const auto &proc_ev = std::get<proc_event>(event);
                    const auto &callback = process_callbacks.at(proc_ev.pid);

                    callback(proc_ev.pid, proc_ev.status);
                    deleteProcess(proc_ev.pid);
                    break;
                }
                case EVTYPE_SOCKET_READ: {
                    const auto &sockfd = std::get<socket_event>(event).sockfd;
                    const auto &callback = socket_read_callbacks.at(sockfd);
                    callback(sockfd);
                    break;
                }
                case EVTYPE_TIMER: {
                    const auto &timer_id = std::get<timer_event>(event).timer_id;
                    const auto &callback = timer_callbacks.at(timer_id);
                    callback();
                    // Not needed because of EV_ONESHOT
                    // impl->ignoreTimer(timer_id);
                    break;
                }
                default:
                    throw std::range_error("type not found");
            }
        }

        void handleFork() {
            impl->handleFork();
        }

    private:

        int getNextTimerId() {
            // Ensure the new ID is not currently being used
            while (timer_callbacks.count(++timer_id_max)) {
                timer_id_max++;
            }
            return timer_id_max;
        }

        std::unordered_map<int, std::function<void(int)>> signal_callbacks;
        std::unordered_map<pid_t, std::function<void(pid_t, int)>> process_callbacks;
        std::unordered_map<int, std::function<void(int)>> socket_read_callbacks;
        std::unordered_map<int, std::function<void()>> timer_callbacks;
        int timer_id_max = 0;
        std::unique_ptr<KernelEventInterface> impl;
    };
}
