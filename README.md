# relaunchd

![Linux CI](https://github.com/mheily/relaunchd/actions/workflows/ci-linux.yml/badge.svg)
[![Coverity](https://scan.coverity.com/projects/8002/badge.svg)](https://scan.coverity.com/projects/mheily-relaunchd)
![CodeQL](https://github.com/mheily/relaunchd/workflows/CodeQL/badge.svg)

## Overview 

relaunchd is an open-source service manager that is similar to the `launchd(8)`
system found in macOS.

It was written from scratch based on the published API, and all code is
available under the ISC license. See the LICENSE file for more information.

It is currently under development, and should not be used for anything
important. Be especially mindful that there is NO WARRANTY provided with this
software.

## Status

See the [release notes](./CHANGELOG.md) for details about
the current release.

relaunchd has been built on the following platforms:
* OpenBSD 7.2
* FreeBSD 13.1-RELEASE
* CentOS Stream 8
* Alpine Linux
* Ubuntu Linux
* MacOS Ventura

The core functionality is working:
* loading and unloading jobs with launchctl
* launching jobs
* periodic jobs that use the StartInterval key
 
Some things are not implemented yet, such as:
* StartCalendar cron emulation
* file and directory watches
* resource limits
* LaunchOnlyOnce
* inetdCompatibility

Some things will probably never be implemented:
* oddities - LimitLoadToHosts, LimitLoadFromHosts
* kernel and launchd debugging - Debug, WaitForDebugger
* Mach IPC
* the StartOnMount key - may require kernel support for filesystem mount
  notifications
* hacks and workarounds - HopefullyExitsFirst, HopefullyExitsLast
* Darwin-specific things - EnableTransactions

## Building from source

CMake is currently the primary build system, but there is a Makefile-based 
buildsystem under development. 

When building on FreeBSD, run the following command to install dependencies:
```
# pkg install nlohmann-json git cmake
```

Required library dependencies:
nlohmann::json is required to build the project.  
on linux, this dependency can be installed via distro's package manager:  
- debian and derivatives: `apt install nlohmann-json3-dev`
- arch and derivatives: `pacman -S nlohmann-json (in community repo)`
- gentoo: `emerge [-a] dev-cpp/nlohmann_json`  

on Macos it can be aquired either via `brew install nlohmann-json` or `port install nlohmann-json`

If you are not able to install `nlohmann::json` in a systemwide location,
you can tell CMake to download and use a private copy. Add the following flag to your CMake
invocation:
```
-DUSE_PRIVATE_NLOHMANN_JSON=YES
```

If you are using an older version of GCC, such as the one
found in CentOS Stream 8, you will need to enable linking
against the external std::filesystem library. Add the following
flag to your CMake invocation:
```
-DUSE_EXTERNAL_CXX17_FILESYSTEM=YES
```

The basic commands to build and install the software are:
```
	mkdir cmake-build-debug
	cd cmake-build-debug
	cmake .. [-Dcmake-args..]
	make
```

### Generating a code coverage report

To generate a code coverage report, run the following:

```
./create-coverage-report.sh
```

## Installation 

To install relaunchd, run the following command in the build directory:

	sudo make install

This will install the following executable commands:
* launchd
* launchctl

It will also install the following manpages: 

* launchctl(1)
* launchd(8)
* launchd.plist(5)

## Usage

After installing relaunchd, you can start the daemon by simply running `launchd`.

## Sanitizers

Currently, only ASAN is supported. To enable ASAN, add
the option `-DENABLE_ASAN=YES` when calling CMake.

## Static Analysis 

[Coverity scan reports](https://scan.coverity.com/projects/mheily-relaunchd?tab=overview)
for relaunchd are available.

## Contact Information

For questions, comments, or other feedback about relaunchd, 
please open an issue on the GitHub page.

## Links

Here are some links to useful information about launchd:
- http://technologeeks.com/docs/launchd.pdf
- http://www.real-world-systems.com/docs/launchctl.1.html
- http://launchd.info/
- https://developer.apple.com/library/mac/technotes/tn2083/_index.html

## References

[1] https://developer.apple.com/library/mac/documentation/Darwin/Reference/ManPages/man8/launchd.8.html
