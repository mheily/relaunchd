cmake_minimum_required(VERSION 3.13)
project(relaunchd VERSION 0.7.0)

# add our own module repository to CMake's search path:
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules ${CMAKE_MODULE_PATH} )

include(CTest)
include(ExternalProject)
include(CheckIncludeFiles)
include(CheckIPOSupported)
include(GNUInstallDirs)
include(CheckSymbolExists)
include(EnableSanitizers)

option(ENABLE_COVERAGE "Should we enable code coverage reports" OFF)

option(ENABLE_LTO "Should we enable link-time optimization?" ON)

option(USE_LIBKQUEUE "Should we use libkqueue" OFF)

option(USE_TINYXML "Should we use TinyXML to support XML manifests" OFF)

option(USE_PRIVATE_DEPENDENCIES
    "Should we fetch, build and link to a private copy of libkqueue?"
    ON)

set(VENDOR_AGENT_LOAD_PATH
        "/lib/relaunchd/LaunchAgents"
        CACHE
        STRING "Search path for system-wide agents provided by the OS vendor")

set(VENDOR_DAEMON_LOAD_PATH
        "/lib/relaunchd/LaunchDaemons"
        CACHE
        STRING "Search path for system-wide daemons provided by the OS vendor")

set(SYSTEM_AGENT_LOAD_PATH
        "/etc/relaunchd/LaunchAgents"
        CACHE
        STRING "Search path for system-wide agents provided by the local system administrators")

set(SYSTEM_DAEMON_LOAD_PATH
        "/etc/relaunchd/LaunchDaemons"
        CACHE
        STRING "Search path for system-wide daemons provided by the local system administrators")

set(USER_AGENT_LOAD_PATH
        "~/.config/relaunchd/LaunchAgents"
        CACHE
        STRING "Search path for user-provided agents")

check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)

CHECK_INCLUDE_FILES(sys/event.h HAVE_SYS_EVENT_H)

if(USE_TINYXML)
    set(XML_MANIFEST_SUPPORT 1)
else()
    set(XML_MANIFEST_SUPPORT 0)
endif()

if(USE_LIBKQUEUE)
    if(USE_PRIVATE_DEPENDENCIES)
        ExternalProject_Add(libkqueue
            GIT_REPOSITORY    https://github.com/mheily/libkqueue.git
            GIT_TAG           v2.6.2
            INSTALL_COMMAND   ""
            )
        ExternalProject_Get_property(libkqueue SOURCE_DIR)
        ExternalProject_Get_property(libkqueue BINARY_DIR)
        # set the variables that find_package() would set:
        set(LibKQueue_INCLUDE_DIR ${SOURCE_DIR}/include)
        set(LibKQueue_LIBRARIES ${BINARY_DIR}/libkqueue.a)
        # redundant:
        set(LibKQueue_LIBRARY_LDFLAGS "")
    else()
        find_package(LibKQueue 2.6.2 REQUIRED)
    endif()
endif()
if(NOT USE_PRIVATE_DEPENDENCIES AND USE_TINYXML)
    find_package(TinyXML2 9.0.0 REQUIRED)
endif()

option(USE_PRIVATE_NLOHMANN_JSON "Should we use a private copy of nlohmann::json?" OFF)
if(USE_PRIVATE_NLOHMANN_JSON)
include(FetchContent)
FetchContent_Declare(json
        URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz)
FetchContent_MakeAvailable(json)
else()
find_package(nlohmann_json 3.2.0 REQUIRED)
endif()

# Enable code coverage automatically for debug builds
if(ENABLE_COVERAGE)
    include(CodeCoverage)
    append_coverage_compiler_flags()
endif()

add_subdirectory(src)
add_subdirectory(test)

#
#  Ensure variables which represent arguments are not
#  cached. This provides more of a sane interface where
#  cmake will build with the arguments you last specified
#  not all the arguments provided since the beginning of
#  the lifetime of CMakeCache.txt.
#
unset(ENABLE_ASAN CACHE)
unset(ENABLE_LSAN CACHE)
unset(ENABLE_UBSAN CACHE)
unset(ENABLE_TSAN CACHE)
