#include <filesystem>
#include <iostream>

#include "manifest.h"
#include "log.h"

using namespace std;

const filesystem::path manifestdir{string{TESTDIR} + "/fixtures"};

void test_parse() {
    using filesystem::directory_iterator;
    for (const auto &file: directory_iterator(manifestdir)) {
        try {
            auto jsondata = manifest::parse(file.path());
            Manifest manifest = jsondata.get<Manifest>();
        } catch (...) {
            cerr << "failed to parse: " << file.path() << endl;
            throw;
        }
    }
}

void testParseComplexDependency() {
    json obj = json::parse(R"(
        [
            {
                "Label": "dep1",
                "StartBefore": true
            }
        ]
    )");
    DependencyList deplist{obj};
}

void testParseSimpleDependency() {
    json obj = json::parse(R"(
        [
            "dep1",
            "dep2"
        ]
    )");
    DependencyList deplist{obj};
}

int main(int argc, char *argv[]) {
    //test_parse();
    testParseComplexDependency();
    testParseSimpleDependency();
}
