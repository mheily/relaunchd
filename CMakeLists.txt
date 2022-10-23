cmake_minimum_required(VERSION 3.13)
project(relaunchd)

# add our own module repository to CMake's search path:
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules ${CMAKE_MODULE_PATH} )

include(ExternalProject)
include(CheckIncludeFiles)
include(GNUInstallDirs)
include(CheckSymbolExists)
include(EnableSanitizers)

option(USE_LIBKQUEUE "Should we use libkqueue" OFF)

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

CHECK_INCLUDE_FILES(sys/event.h HAVE_SYS_EVENT_H)

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
if(NOT USE_PRIVATE_DEPENDENCIES)
    find_package(TinyXML2 9.0.0 REQUIRED)
endif()

add_subdirectory(man)
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
