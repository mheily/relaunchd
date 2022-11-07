#undef NDEBUG

#include <cassert>
#include <filesystem>
#include <iostream>

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
          "Program": "/bin/true",
          "RunAtLoad": true,
          "Dependencies": [
            "test.job2"
          ]
        }
    )");
    json job2_manifest = json::parse(R"(
        {
          "Label": "test.job2",
          "Program": "/bin/true"
        }
    )");
    std::string path = "/dev/null";
    mgr.loadManifest(job1_manifest, path);
    mgr.loadManifest(job2_manifest, path);
    mgr.startAllJobs();
    auto &job1 = mgr.getJob("test.job1");
    auto &job2 = mgr.getJob("test.job2");
    assert(job1.hasStarted());
    assert(job2.hasStarted());
    assert(mgr.handleEvent());      // event: reap the PID of a job
    assert(mgr.handleEvent());      // event: reap the PID of a job
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
          "Program": "/bin/true",
          "RunAtLoad": true,
          "Dependencies": [
            "test.job2"
          ]
        }
    )");
    json job2_manifest = json::parse(R"(
        {
          "Label": "test.job2",
          "Program": "/bin/true",
          "Dependencies": [
            "test.job1"
          ]
        }
    )");
    std::string path = "/dev/null";
    mgr.loadManifest(job1_manifest, path);
    mgr.loadManifest(job2_manifest, path);
    mgr.startAllJobs();
    auto &job1 = mgr.getJob("test.job1");
    auto &job2 = mgr.getJob("test.job2");
    assert(!job1.hasStarted());
    assert(!job2.hasStarted());
    assert(job1.state == JOB_STATE_MISSING_DEPENDS);
    assert(job2.state == JOB_STATE_MISSING_DEPENDS);
}

//! Test the job.shouldStart() logic
void testShouldStart() {
    //log_freopen(stdout);
    Manager mgr{DOMAIN_TYPE_USER};
    json job1_manifest = json::parse(R"(
        {
          "Label": "test.job1",
          "Program": "/bin/true",
          "RunAtLoad": true
        }
    )");
    json job2_manifest = json::parse(R"(
        {
          "Label": "test.job2",
          "Program": "/bin/true",
          "StartInterval": 60
        }
    )");
    std::string path = "/dev/null";
    mgr.loadManifest(job1_manifest, path);
    mgr.loadManifest(job2_manifest, path);
    auto &job1 = mgr.getJob("test.job1");
    auto &job2 = mgr.getJob("test.job2");
    assert(job1.shouldStart());
    assert(job2.shouldStart());
    mgr.startAllJobs();
    assert(job1.hasStarted());
    assert(job2.hasStarted());
    assert(job1.state == JOB_STATE_RUNNING);
    assert(job2.state == JOB_STATE_WAITING);
}

int main(int argc, char *argv[]) {
    testCyclicDependency();
    testDependencies();
    testShouldStart();
}
