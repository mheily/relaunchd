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

#undef NDEBUG

#include <cassert>
#include <filesystem>
#include <iostream>

#include "common.hpp"
#include "manager.h"
#include "log.h"

using namespace std;

//const filesystem::path manifestdir{string{TESTDIR} + "/fixtures"};
//
//void test_parse() {
//    using filesystem::directory_iterator;
//    for (const auto &file: directory_iterator(manifestdir)) {
//        try {
//            auto jsondata = manifest::parse(file.path());
//            Manifest manifest = jsondata.get<Manifest>();
//        } catch (...) {
//            cerr << "failed to parse: " << file.path() << endl;
//            throw;
//        }
//    }
//}

void testDependencies() {
    //log_freopen(stdout);
    Manager mgr{DOMAIN_TYPE_USER};
    json job1_manifest = json::parse(R"(
        {
          "Label": "test.job1",
          "Program": "/bin/sh",
          "RunAtLoad": true,
          "Dependencies": [
            "test.job2"
          ]
        }
    )");
    json job2_manifest = json::parse(R"(
        {
          "Label": "test.job2",
          "Program": "/bin/sh"
        }
    )");
    std::string path = "/dev/null";
    mgr.loadManifest(job1_manifest, path);
    mgr.loadManifest(job2_manifest, path);
    mgr.startAllJobs();
    auto &job1 = mgr.getJob({"test.job1"});
    auto &job2 = mgr.getJob({"test.job2"});
    assert(job1.hasStarted());
    assert(job2.hasStarted());
    assert(mgr.handleEvent(std::chrono::milliseconds{100}));      // event: reap the PID of a job
    assert(mgr.handleEvent(std::chrono::milliseconds{100}));      // event: reap the PID of a job
    assert(0 == job1.last_exit_status);
    assert(0 == job1.pid);
    assert(0 == job2.last_exit_status);
    assert(0 == job2.pid);
    assert(job1.state == JOB_STATE_EXITED);
    assert(job2.state == JOB_STATE_EXITED);
}

void testCyclicDependency() {
    //log_freopen(stdout);
    Manager mgr{DOMAIN_TYPE_USER};
    json job1_manifest = json::parse(R"(
        {
          "Label": "test.job1",
          "Program": "/bin/sh",
          "RunAtLoad": true,
          "Dependencies": [
            "test.job2"
          ]
        }
    )");
    json job2_manifest = json::parse(R"(
        {
          "Label": "test.job2",
          "Program": "/bin/sh",
          "Dependencies": [
            "test.job1"
          ]
        }
    )");
    std::string path = "/dev/null";
    mgr.loadManifest(job1_manifest, path);
    mgr.loadManifest(job2_manifest, path);
    mgr.startAllJobs();
    auto &job1 = mgr.getJob({"test.job1"});
    auto &job2 = mgr.getJob({"test.job2"});
    assert(!job1.hasStarted());
    assert(!job2.hasStarted());
    assert(job1.state == JOB_STATE_MISSING_DEPENDS);
    assert(job2.state == JOB_STATE_MISSING_DEPENDS);
    mgr.unloadAllJobs();
}

//! Test what happens if a dependency does not exist
void testMissingDependency() {
    //log_freopen(stdout);
    Manager mgr{DOMAIN_TYPE_USER};
    json job1_manifest = json::parse(R"(
        {
          "Label": "test.job1",
          "Program": "/bin/sh",
          "RunAtLoad": true,
          "Dependencies": [
            "--this-job-does-not-exist--"
          ]
        }
    )");
    std::string path = "/dev/null";
    mgr.loadManifest(job1_manifest, path);
    mgr.startAllJobs();
    auto &job1 = mgr.getJob({"test.job1"});
    assert(!job1.hasStarted());
    assert(job1.state == JOB_STATE_MISSING_DEPENDS);
}

//! Verify that ThrottleInterval works
void testThrottleInterval() {
    //log_freopen(stdout);
    Manager mgr{DOMAIN_TYPE_USER};
    json job1_manifest = json::parse(R"(
        {
          "Label": "test.job1",
          "Program": "/bin/sh",
          "RunAtLoad": true,
          "KeepAlive": true,
          "ThrottleInterval": 1
        }
    )");
    std::string path = "/dev/null";
    mgr.loadManifest(job1_manifest, path);
    mgr.startAllJobs();
    auto &job1 = mgr.getJob({"test.job1"});
    assert(job1.hasStarted());
    assert(job1.state == JOB_STATE_RUNNING);
    pid_t old_pid = job1.pid;
    assert(mgr.handleEvent());      // event: reap the PID of the job
    auto &job2 = mgr.getJob({"test.job1"});
    assert(job2.state == JOB_STATE_WAITING);
    sleep(2);
    assert(mgr.handleEvent());      // event: timer expires due to ThrottleInterval, job restarts
    auto &job3 = mgr.getJob({"test.job1"});
    assert(job3.state == JOB_STATE_RUNNING);
    assert(job3.pid != old_pid != 0);
}

//! Test the job.shouldStart() logic
void testShouldStart() {
    //log_freopen(stdout);
    Manager mgr{DOMAIN_TYPE_USER};
    json job1_manifest = json::parse(R"(
        {
          "Label": "test.job1",
          "Program": "/bin/sh",
          "RunAtLoad": true
        }
    )");
    json job2_manifest = json::parse(R"(
        {
          "Label": "test.job2",
          "Program": "/bin/sh",
          "StartInterval": 60
        }
    )");
    std::string path = "/dev/null";
    mgr.loadManifest(job1_manifest, path);
    mgr.loadManifest(job2_manifest, path);
    auto &job1 = mgr.getJob({"test.job1"});
    auto &job2 = mgr.getJob({"test.job2"});
    assert(job1.shouldStart());
    assert(job2.shouldStart());
    mgr.startAllJobs();
    assert(job1.hasStarted());
    assert(job2.hasStarted());
    assert(job1.state == JOB_STATE_RUNNING);
    assert(job2.state == JOB_STATE_WAITING);
}

void testKeepaliveAfterExit() {
    //log_freopen(stdout);
    Manager mgr{DOMAIN_TYPE_USER};
    json manifest = json::parse(R"(
        {
          "Label": "test.job1",
          "Program": "/bin/sh",
          "RunAtLoad": true,
          "KeepAlive": true,
          "ThrottleInterval": 0
        }
    )");
    std::string path = "/dev/null";
    mgr.loadManifest(manifest, path);
    mgr.startAllJobs();
    auto &job = mgr.getJob({"test.job1"});
    assert(job.state == JOB_STATE_RUNNING);
    pid_t old_pid = job.pid;
    mgr.handleEvent();
    assert(old_pid != job.pid);
}

void testKeepaliveAfterSignal() {
    //log_freopen(stderr);
    Manager mgr{DOMAIN_TYPE_USER};
    assert(std::filesystem::exists("/bin/sleep"));
    json manifest = json::parse(R"(
        {
          "Label": "test.job1",
          "ProgramArguments": ["/bin/sleep", "10"],
          "RunAtLoad": true,
          "KeepAlive": true,
          "ThrottleInterval": 0
        }
    )");
    std::string path = "/dev/null";
    mgr.loadManifest(manifest, path);
    mgr.startAllJobs();
    auto &job = mgr.getJob({"test.job1"});
    assert(job.state == JOB_STATE_RUNNING);
    pid_t old_pid = job.pid;
    assert(mgr.killJob({"test.job1"}, "SIGKILL"));
    mgr.handleEvent(std::chrono::milliseconds{100});
    assert(job.state == JOB_STATE_RUNNING);
    assert(old_pid != job.pid);
}

void testKillJobBySignal() {
    Manager mgr{DOMAIN_TYPE_USER};
    assert(std::filesystem::exists("/bin/sleep"));
    json manifest = json::parse(R"(
        {
          "Label": "test.job1",
          "ProgramArguments": ["/bin/sleep", "9999"],
          "RunAtLoad": true
        }
    )");
    Label label{"test.job1"};
    std::string path = "/dev/null";
    mgr.loadManifest(manifest, path);
    mgr.startAllJobs();
    assert(!mgr.killJob(label, "A bad signal name that does not exist"));
    assert(mgr.killJob(label, "SIGKILL"));
    assert(mgr.killJob(label, "9"));
    mgr.handleEvent();
    auto &job = mgr.getJob(label);
    assert(job.state == JOB_STATE_EXITED);
    assert(job.last_exit_status == -1);
    assert(job.term_signal == 9);
}

void addManagerTests(TestRunner &runner) {
    runner.addTest("testKeepaliveAfterSignal", testKeepaliveAfterSignal);
    runner.addTest("testKeepaliveAfterExit", testKeepaliveAfterExit);
    runner.addTest("testCyclicDependency", testCyclicDependency);
    runner.addTest("testMissingDependency", testMissingDependency);
    runner.addTest("testDependencies", testDependencies);
    runner.addTest("testShouldStart", testShouldStart);
    runner.addTest("testThrottleInterval", testThrottleInterval);
    runner.addTest("testKillJobBySignal", testKillJobBySignal);
}
