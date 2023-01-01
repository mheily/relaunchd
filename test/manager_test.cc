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
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

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

Manager getManager() {
    Domain domain{DomainType::User, TMPDIR};
    auto statefile = domain.statedir / "state.json";
    if (std::filesystem::exists(statefile)) {
        std::filesystem::remove(statefile);
    }
    return Manager{domain};
}

void testDependencies() {
    //log_freopen(stdout);
    auto mgr = getManager();
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
    assert(job1.state == job_state::exited);
    assert(job2.state == job_state::exited);
}

void testCyclicDependency() {
    //log_freopen(stdout);
    auto mgr = getManager();
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
    assert(job1.state == job_state::missing_depends);
    assert(job2.state == job_state::missing_depends);
    mgr.unloadAllJobs();
}

//! Test what happens if a dependency does not exist
void testMissingDependency() {
    //log_freopen(stdout);
    auto mgr = getManager();
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
    assert(job1.state == job_state::missing_depends);
}

//! Verify that ThrottleInterval works
void testThrottleInterval() {
    //log_freopen(stdout);
    auto mgr = getManager();
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
    assert(job1.state == job_state::running);
    pid_t old_pid = job1.pid;
    assert(mgr.handleEvent());      // event: reap the PID of the job
    auto &job2 = mgr.getJob({"test.job1"});
    assert(job2.state == job_state::waiting);
    sleep(2);
    assert(mgr.handleEvent());      // event: timer expires due to ThrottleInterval, job restarts
    auto &job3 = mgr.getJob({"test.job1"});
    assert(job3.state == job_state::running);
    assert((job3.pid != old_pid) != 0);
}

//! Test the job.shouldStart() logic
void testShouldStart() {
    //log_freopen(stdout);
    auto mgr = getManager();
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
    assert(job1.state == job_state::running);
    assert(job2.state == job_state::waiting);
}

void testKeepaliveAfterExit() {
    //log_freopen(stdout);
    auto mgr = getManager();
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
    assert(job.state == job_state::running);
    pid_t old_pid = job.pid;
    mgr.handleEvent();
    assert(old_pid != job.pid);
}

void testKeepaliveAfterSignal() {
    //log_freopen(stderr);
    auto mgr = getManager();
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
    assert(job.state == job_state::running);
    pid_t old_pid = job.pid;
    assert(mgr.killJob({"test.job1"}, "SIGKILL"));
    mgr.handleEvent(std::chrono::milliseconds{100});
    assert(job.state == job_state::running);
    assert(old_pid != job.pid);
}

void testKillJobBySignal() {
    auto mgr = getManager();
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
    assert(job.state == job_state::exited);
    assert(job.last_exit_status == -1);
    assert(job.term_signal == 9);
}


void testUnload() {
    auto mgr = getManager();
    assert(!mgr.unloadJob(Label{"a job path that does not exist"}));
    assert(!mgr.unloadJob(Label{"a job label that does not exist"}));
    Label label{"test.job1"};
    json manifest = json::parse(R"(
        {
          "Label": "test.job1",
          "ProgramArguments": ["/bin/sh", "-c", "sleep 12"],
          "RunAtLoad": true
        }
    )");
    std::string path = "/dev/null";
    mgr.loadManifest(manifest, path);
    mgr.startAllJobs();
    assert(mgr.unloadJob(label));
    assert(!mgr.unloadJob(label));
    assert(mgr.handleEvent(std::chrono::milliseconds{100}));
    assert(!mgr.jobExists(label));
// TODO: test load/unload with overridedisabled and forceunload
}

void testUnloadWithOverrideDisabled() {
    auto mgr = getManager();
    Label label{"testUnloadWithOverrideDisabled"};
    json manifest = json::parse(R"(
        {
          "Label": "testUnloadWithOverrideDisabled",
          "ProgramArguments": ["/bin/sh", "-c", "sleep 12"],
          "RunAtLoad": true,
          "Disabled": true
        }
    )");
    std::string path = "/dev/null";
    mgr.loadManifest(manifest, path);
    // Since it was disabled, the job should not exist.
    assert(!mgr.jobExists(label));
    mgr.loadManifest(manifest, path, true, true);
    assert(mgr.jobExists(label));
    mgr.startAllJobs();
    mgr.unloadJob(label, true, true);
    assert(mgr.handleEvent(std::chrono::milliseconds{100}));
    assert(!mgr.jobExists(label));
}

// Ensure that the process group is killed when AbandonProcessGroup == false
void testAbandonProcessGroup() {
    auto mgr = getManager();
    Label label{"testAbandonProcessGroup"};
    std::filesystem::path pidfile = tmpdir + "/" + static_cast<std::string>(label) + ".pid";
    json manifest = json{
            {"Label", label},
            {"AbandonProcessGroup", false},
            {"ProgramArguments", json::array({
                "/bin/sh", "-c",
                "nohup sleep 291 &\n"
                "echo $! > " + static_cast<std::string>(pidfile),
            })},
            {"RunAtLoad", true}
    };
    std::string path = "/dev/null";
    mgr.loadManifest(manifest, path);
    mgr.startAllJobs();
    assert(mgr.handleEvent(std::chrono::milliseconds{500}));
    auto &job = mgr.getJob(label);
    assert(job.state == job_state::exited);

    // Verify the subprocess was killed
    assert(std::filesystem::exists(pidfile));
    std::ifstream ifs(pidfile);
    std::ostringstream line;
    line << ifs.rdbuf();
    pid_t pid = std::stoi(line.str());
    assert(killpg(pid, SIGTERM) == -1 && errno == ESRCH);
}

// Test the jobHasReverseDependencies() method.
void testJobHasReverseDependencies() {
    auto mgr = getManager();
    Label labelA{"jobHasReverseDependencies.A"};
    Label labelB{"jobHasReverseDependencies.B"};
    json manifestA = json{
            {"Label", labelA},
            {"Program", "/bin/sh"},
            {"Dependencies", json::array({labelB.str()})},
    };
    json manifestB = json{
            {"Label", labelB},
            {"Program", "/bin/sh"},
    };
    std::string path = "/dev/null";
    mgr.loadManifest(manifestA, path);
    mgr.loadManifest(manifestB, path);
    auto &jobA = mgr.getJob(labelA);
    auto &jobB = mgr.getJob(labelB);
    assert(!mgr.jobHasReverseDependencies(jobA));
    assert(mgr.jobHasReverseDependencies(jobB));
    mgr.unloadJob(jobA);
    assert(!mgr.jobHasReverseDependencies(jobB));
}

void addManagerTests(TestRunner &runner) {
#define X(y) runner.addTest("" # y, y)
    X(testJobHasReverseDependencies);
    X(testAbandonProcessGroup);
    X(testUnloadWithOverrideDisabled);
    X(testUnload);
    X(testKeepaliveAfterSignal);
    X(testKeepaliveAfterExit);
    X(testCyclicDependency);
    X(testMissingDependency);
    X(testDependencies);
    X(testShouldStart);
    X(testThrottleInterval);
    X(testKillJobBySignal);
#undef X
}
