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
#include <optional>
#include <queue>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#elif __has_include(<sys/event.h>)
#define USE_KQUEUE 1
#include <sys/event.h>
#ifndef NOTE_EXITSTATUS
#define NOTE_EXITSTATUS 0
#endif
#else
#error No supported kernel event API detected
#endif

#include <chrono>
#include <functional>
#include <iostream>
#include <queue>
#include <stdexcept>
#include <unordered_set>
#include <variant>
#include <vector>

#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
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
} // namespace kqtrace

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

struct ipc_event {
    std::string method, arg;
};

// N.B. event_type must be kept in sync with the std::variant below.
typedef std::variant<proc_event, signal_event, socket_event, timer_event,
                     ipc_event>
    Event;
enum event_type {
    EVTYPE_PROC,
    EVTYPE_SIGNAL,
    EVTYPE_SOCKET_READ,
    EVTYPE_TIMER,
    EVTYPE_IPC,
    EVTYPE_NONE = 32,
};

namespace kq::error {
class ProcessNotFound : public std::exception {};
}; // namespace kq::error

class KernelEventInterface {
  public:
    virtual ~KernelEventInterface() = default;

    virtual std::optional<Event>
    waitForEvent(std::optional<std::chrono::milliseconds> timeout) = 0;

    virtual void monitorChildProcess(pid_t pid) = 0;

    virtual void ignoreChildProcess(pid_t pid) = 0;

    virtual void monitorSignal(int signum) = 0;

    virtual void unblockSignal(int signum) = 0;

    virtual void unblockAllSignals() noexcept = 0;

    virtual void ignoreSignal(int signum) = 0;

    virtual void monitorSocketRead(int sockfd) = 0;

    virtual void ignoreSocketRead(int sockfd) = 0;

    virtual void monitorTimer(int timer_id, uint64_t milliseconds) = 0;

    virtual void ignoreTimer(int timer_id) = 0;

    void addPendingEvent(Event evt) { pending_events.emplace(std::move(evt)); }

    std::optional<Event> getPendingEvent() {
        std::optional<Event> result;
        if (!child_status.empty()) {
            const auto &it = child_status.begin();
            result = proc_event{it->first, it->second};
            child_status.erase(it);
        } else if (!pending_events.empty()) {
            result = std::move(pending_events.front());
            pending_events.pop();
        }
        return result;
    }

    int waitForProcess(pid_t pid) {
        int wstatus, rv;
        rv = waitpid(pid, &wstatus, WNOHANG);
        if (rv == 0 || rv == -1) {
            std::string msg = std::string{"waitpid failed: retval="} +
                              std::to_string(rv) + std::string{" errno="} +
                              std::to_string(errno);
            kqtrace::print(msg);
            return -1;
        }
        return wstatus;
    }

    //! Run cleanup actions between fork() and exec(), such as resetting signal
    //! handlers
    virtual void handleFork() = 0;

    //! waitpid(2) status information for reaped processes.
    std::unordered_map<pid_t, int> child_status;

    //! signal handlers to restore when cleaning up
    std::unordered_set<int> blocked_signals;

    //! events that have occurred but not yet returned by waitForEvent()
    std::queue<Event> pending_events;

    // TODO: make this process-wide, since all threads share the same signal
    // handlers. Think about whether we want to allow multiple
    // KernelEventInterface objects at all, as only one of them could
    // realistically handle SIGCHLD and other signals. Maybe allow one object to
    // manage signals, and any other objects will not be able to manage signals:
    //    static std::mutex signal_mtx;
    //    bool supports_signals = signal_mtx.try_lock();
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
            throw std::system_error(errno, std::system_category(),
                                    "signalfd()");
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
        for (auto &fd : cleanup_fds) {
            close(fd);
        }
        unblockAllSignals();
    }

    void handleFork() override { unblockAllSignals(); }

    std::optional<Event>
    waitForEvent(std::optional<std::chrono::milliseconds> timeout) override {
        std::optional<Event> result = getPendingEvent();
        if (result.has_value()) {
            return result;
        }
        for (;;) {
            int evtype;
            auto maybe_event = epollGetOne(epfd, timeout);
            if (maybe_event) {
                const auto &event = *maybe_event;
                evtype = event.data.u32;
            } else {
                evtype = EVTYPE_NONE;
            }
            switch (evtype) {
            case EVTYPE_NONE: {
                kqtrace::print("epollGetOne() did not return an event");
            } break;
            case EVTYPE_SIGNAL: {
                getSignalEvents();
                if (!pending_events.empty()) {
                    auto event = pending_events.front();
                    pending_events.pop();
                    return event;
                } else {
                    kqtrace::print("spurious wake: expected a signal event, "
                                   "but none occurred");
                }
            } break;
            case EVTYPE_SOCKET_READ: {
                auto event = epollGetOne(socket_read_fd, timeout);
                if (event) {
                    return Event(
                        socket_event{static_cast<int>(event->data.fd)});
                } else {
                    kqtrace::print(
                        "spurious wakeup: epollGetOne(socket_read_fd) did not "
                        "return an event");
                }
            } break;
            case EVTYPE_TIMER: {
                auto event = epollGetOne(timer_epfd, timeout);
                if (event) {
                    uint64_t expired;
                    const auto &fd = event->data.fd;
                    ssize_t n = read(fd, &expired, sizeof(expired));
                    if (n != sizeof(expired)) {
                        if (errno == EAGAIN) {
                            kqtrace::print("spurious timer wakeup");
                            break;
                        } else {
                            throw std::system_error(
                                errno, std::system_category(), "read()");
                        }
                    }
                    (void)expired; // We don't return this to the caller, but
                                   // should in the future.
                    for (const auto &[timer_id, tfd] : timerfd_map) {
                        if (tfd == fd) {
                            return Event(timer_event{timer_id});
                        }
                    }
                    throw std::range_error(
                        "timer FD does not match any known timer IDs");
                } else {
                    kqtrace::print("spurious wake: expected a timer event, but "
                                   "none occurred");
                }
            } break;
            default:
                throw std::range_error("invalid filter");
            }
            if (timeout.has_value()) {
                break;
            }
        }
        return std::nullopt;
    }

    void monitorChildProcess(pid_t pid) override {
        // We could use pidfd_open(2) but this requires Linux 5.3+
        // For now, just use SIGCHLD and assume that all children will be reaped
        // via SIGCHLD handling
        if (kill(pid, 0) != 0) {
            if (errno == ESRCH) {
                throw kq::error::ProcessNotFound();
            } else {
                throw std::system_error(errno, std::system_category(),
                                        "kill()");
            }
        }
        // <---- TODO: race condition here, hard to avoid unless the child waits
        // for the parent to start monitoring. maybe just stop using watch_pids?
        watch_pids.insert(pid);
    }

    void ignoreChildProcess(pid_t pid) override { watch_pids.erase(pid); }

    void monitorSocketRead(int sockfd) override {
        // TODO: use getsockopt() to verify this is a socket
        struct epoll_event epev;
        epev.events = EPOLLIN;
        epev.data.fd = sockfd;
        if (epoll_ctl(socket_read_fd, EPOLL_CTL_ADD, sockfd, &epev) < 0) {
            throw std::system_error(errno, std::system_category(),
                                    "epoll_ctl()");
        }
    }

    void ignoreSocketRead(int sockfd) override {
        kqtrace::print("deleting epoll watch for sd " + std::to_string(sockfd));
        if (epoll_ctl(socket_read_fd, EPOLL_CTL_DEL, sockfd, nullptr) < 0) {
            throw std::system_error(errno, std::system_category(),
                                    "epoll_ctl()");
        }
    }

    void monitorSignal(int signum) override {
        sigaddset(&sigmask, signum);
        if (signalfd(sigfd, &sigmask, 0) < 0) {
            throw std::system_error(errno, std::system_category(),
                                    "signalfd()");
        }
        changeSignalMask(SIG_BLOCK, signum);
        blocked_signals.insert(signum);
    }

    void unblockSignal(int signum) override {
        changeSignalMask(SIG_UNBLOCK, signum);
        blocked_signals.erase(signum);
    }

    void unblockAllSignals() noexcept override {
        sigset_t mask;
        sigemptyset(&mask);
        for (int signum : blocked_signals) {
            sigaddset(&mask, signum);
        }
        (void)sigprocmask(SIG_UNBLOCK, &mask, NULL);
        blocked_signals.clear();
    }

    void ignoreSignal(int signum) override {
        sigdelset(&sigmask, signum);
        if (signalfd(sigfd, &sigmask, 0) < 0) {
            throw std::system_error(errno, std::system_category(),
                                    "signalfd()");
        }
        unblockSignal(signum);
    }

    void monitorTimer(int timer_id, uint64_t milliseconds) override {
        int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        if (tfd < 0) {
            throw std::system_error(errno, std::system_category(),
                                    "signalfd()");
        }
        cleanup_fds.insert(tfd);
        // Convert milliseconds to seconds+milliseconds
        int tv_sec = milliseconds / 1000;
        int tv_nsec = (milliseconds % 1000) * 1000000;
        struct timespec ts = {tv_sec, tv_nsec};

        struct itimerspec its = {{0, 0}, ts};
        if (timerfd_settime(tfd, 0, &its, NULL) < 0) {
            throw std::system_error(errno, std::system_category(),
                                    "timerfd_settime()");
        }

        struct epoll_event epev;
        epev.events = EPOLLIN;
        epev.data.u32 = tfd;
        if (epoll_ctl(timer_epfd, EPOLL_CTL_ADD, tfd, &epev) < 0) {
            throw std::system_error(errno, std::system_category(),
                                    "timerfd_settime()");
        }
        timerfd_map.insert({{timer_id, tfd}});
    }

    void ignoreTimer(int timer_id) override {
        int tfd = timerfd_map.at(timer_id);
        int rv = epoll_ctl(epfd, EPOLL_CTL_DEL, tfd, nullptr);
        int saved_errno = errno;
        (void)close(tfd); // FIXME: err handling
        timerfd_map.erase(timer_id);
        cleanup_fds.erase(tfd);
        if (rv < 0) {
            errno = saved_errno;
            throw std::system_error(errno, std::system_category(),
                                    "epoll_ctl()");
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
                    kqtrace::print(
                        "no more signal events can be read from the signalfd");
                    break;
                } else {
                    throw std::system_error(errno, std::system_category(),
                                            "read()");
                }
            }
            if (sig.ssi_signo == SIGCHLD) {
                sigchild_seen = true;
            } else {
                pending_events.emplace(
                    Event(signal_event{static_cast<int>(sig.ssi_signo)}));
            }
        }
        // Reap all zombies and create process events
        if (sigchild_seen) {
            kqtrace::print("special case: handling one or more SIGCHLD events");
            for (;;) {
                int status;
                pid_t pid = waitpid(-1, &status, WNOHANG);
                if (pid == 0) {
                    break;
                } else if (pid < 0) {
                    if (errno == ECHILD) {
                        break;
                    } else {
                        throw std::system_error(errno, std::system_category(),
                                                "waitpid()");
                    }
                }
                if (watch_pids.count(pid)) {
                    pending_events.emplace(Event(proc_event{pid, status}));
                    watch_pids.erase(pid);
                } else {
                    kqtrace::print("pid " + std::to_string(pid) +
                                   " exited but it was not being watched");
                    continue;
                }
            }
        }
    }

    static void changeSignalMask(int how, int signum) {
        assert(how == SIG_BLOCK || how == SIG_UNBLOCK || how == SIG_SETMASK);
        sigset_t delta;
        sigemptyset(&delta);
        sigaddset(&delta, signum);
        (void)sigprocmask(how, &delta, NULL);
    }

    static int epollCreate() {
        int fd = epoll_create(10);
        if (fd < 0) {
            throw std::system_error(errno, std::system_category(),
                                    "epoll_create()");
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
            throw std::system_error(errno, std::system_category(),
                                    "epoll_ctl()");
        }
    }

    std::optional<epoll_event>
    epollGetOne(int fd, std::optional<std::chrono::milliseconds> timeout) {
        struct epoll_event event;
        int eptimeout = timeout ? (int)(timeout->count()) : -1;
        int rv = epoll_wait(fd, &event, 1, eptimeout);
        if (rv > 0) {
            return event;
        } else if (rv == 0) {
            return std::nullopt;
        } else {
            throw std::system_error(errno, std::system_category(),
                                    "epoll_wait()");
        }
    }

    int epfd = -1;
    int sigfd = -1;
    int socket_read_fd = -1;
    int timer_epfd = -1;
    sigset_t sigmask;                   // signals to catch
    std::unordered_set<int> watch_pids; // process IDs to monitor
    std::unordered_map<int, int> timerfd_map;
    std::unordered_set<int>
        cleanup_fds; // file descriptors to close via the destructor
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

    ~KqueueImplementation() { close(kqfd); }

    std::optional<Event>
    waitForEvent(std::optional<std::chrono::milliseconds> timeout) override {
        std::optional<Event> result = getPendingEvent();
        if (result.has_value()) {
            return result;
        }
        struct kevent kev;
        for (;;) {
            struct timespec ts;
            struct timespec *tsptr;
            if (timeout) {
                using namespace std::chrono;
                auto secs = duration_cast<seconds>(timeout.value());
                auto nanosecs =
                    duration_cast<nanoseconds>(timeout.value() - secs);
                ts = {secs.count(), nanosecs.count()};
                tsptr = &ts;
            } else {
                tsptr = nullptr;
            }
            int rv = kevent(kqfd, NULL, 0, &kev, 1, tsptr);
            if (rv > 0) {
                break;
            } else if (rv == 0) {
                if (timeout) {
                    return std::nullopt;
                } else {
                    continue;
                }
            } else {
                if (errno == EINTR) {
                    kqtrace::print(
                        "kevent() was interrupted by a signal, will continue");
                    continue;
                }
                throw std::system_error(errno, std::system_category(),
                                        "kevent()");
            }
        }
        switch (kev.filter) {
        case EVFILT_PROC:
            return Event(proc_event{static_cast<pid_t>(kev.ident),
                                    static_cast<int>(kev.data)});
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
        try {
            changeKevent(pid, EVFILT_PROC, EV_ADD | EV_ONESHOT,
                         NOTE_EXIT | NOTE_EXITSTATUS);
        } catch (const std::system_error &e) {
            if (e.code().value() == ESRCH) {
                // The process has already exited.
                child_status.emplace(pid, waitForProcess(pid));
            } else {
                throw;
            }
        }
    }

    void ignoreChildProcess(pid_t pid) override {
        auto it = child_status.find(pid);
        if (it == child_status.end()) {
            try {
                changeKevent(pid, EVFILT_PROC, EV_DELETE, NOTE_EXIT);
            } catch (const std::system_error &e) {
                if (e.code().value() != ENOENT) {
                    throw;
                }
            }
        }
    }

    void monitorSocketRead(int sockfd) override {
        changeKevent(sockfd, EVFILT_READ, EV_ADD, NOTE_EXIT);
    }

    void ignoreSocketRead(int sockfd) override {
        changeKevent(sockfd, EVFILT_READ, EV_DELETE, 0);
    }

    void monitorSignal(int signum) override {
        if (signum == SIGCHLD) {
            throw std::range_error(
                "SIGCHLD is handled internally to support Linux");
        }
        if (signal(signum, SIG_IGN) == SIG_ERR) {
            throw std::system_error(errno, std::system_category(), "signal()");
        }
        blocked_signals.insert(signum);
        changeKevent(signum, EVFILT_SIGNAL, EV_ADD, NOTE_EXIT);
    }

    void unblockSignal(int signum) override {
        if (signal(signum, SIG_DFL) == SIG_ERR) {
            throw std::system_error(errno, std::system_category(), "signal()");
        }
        blocked_signals.erase(signum);
    }

    void unblockAllSignals() noexcept override {
        for (int signum : blocked_signals) {
            (void)signal(signum, SIG_DFL);
        }
        blocked_signals.clear();
    }

    void ignoreSignal(int signum) override {
        unblockSignal(signum);
        changeKevent(signum, EVFILT_SIGNAL, EV_DELETE, NOTE_EXIT);
    }

    void monitorTimer(int timer_id, uint64_t milliseconds) override {
        changeKevent(timer_id, EVFILT_TIMER, EV_ADD | EV_ONESHOT, 0,
                     milliseconds);
    }

    void ignoreTimer(int timer_id) override {
        changeKevent(timer_id, EVFILT_TIMER, EV_DELETE, 0);
    }

    void handleFork() override { unblockAllSignals(); }

  private:
    void changeKevent(uintptr_t ident, int filter, int flags, int fflags,
                      uint64_t data = 0) {
        struct kevent kev;
        EV_SET(&kev, ident, filter, flags, fflags, data, NULL);
        if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0) {
            throw std::system_error(errno, std::system_category(), "kevent()");
        }
    }
    int kqfd = -1;
};

#endif // USE_KQUEUE

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

    void deleteSocketRead(int sd) {
        impl->ignoreSocketRead(sd);
        socket_read_callbacks.erase(sd);
    }

    int addTimer(const std::chrono::milliseconds milliseconds,
                 std::function<void()> callback) {
        int timer_id = getNextTimerId();
        kqtrace::print("adding timer for " +
                       std::to_string(milliseconds.count()) + "ms");
        impl->monitorTimer(timer_id, milliseconds.count());
        timer_callbacks.insert({{timer_id, callback}});
        return timer_id;
    }

    void addIpcMethod(std::string name,
                      std::function<void(std::string)> callback) {
        ipc_callbacks.insert({std::move(name), std::move(callback)});
    }

    void submitIpcCallback(std::string method, std::string arg) {
        impl->addPendingEvent(ipc_event{std::move(method), std::move(arg)});
    }

    void waitForEvent(std::optional<std::chrono::milliseconds> timeout) {
        std::optional<Event> maybe_event = impl->waitForEvent(timeout);
        if (!maybe_event) {
            return;
        }
        const Event &event = maybe_event.value();
        kqtrace::print("got an event of type " + std::to_string(event.index()));
        switch (event.index()) {
        case EVTYPE_IPC: {
            const auto &ipc_ev = std::get<ipc_event>(event);
            const auto &callback = ipc_callbacks.at(ipc_ev.method);
            callback(ipc_ev.arg);
            // Not needed because IPC methods persist until manually deleted.
            // impl->ignoreCallback(ipc_ev.method);
            break;
        }
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

    void handleFork() { impl->handleFork(); }

  private:
    int getNextTimerId() {
        // Ensure the new ID is not currently being used
        while (timer_callbacks.count(++timer_id_max)) {
            timer_id_max++;
        }
        return timer_id_max;
    }

    std::unordered_map<int, std::function<void(int)>> signal_callbacks;
    std::unordered_map<pid_t, std::function<void(pid_t, int)>>
        process_callbacks;
    std::unordered_map<int, std::function<void(int)>> socket_read_callbacks;
    std::unordered_map<int, std::function<void()>> timer_callbacks;
    std::unordered_map<std::string, std::function<void(std::string)>>
        ipc_callbacks;
    int timer_id_max = 0;
    std::unique_ptr<KernelEventInterface> impl;
};
} // namespace kq
