cmake_minimum_required(VERSION 3.10)
project(relaunchd)

include(ExternalProject)
include(CheckIncludeFiles)

option(USE_PRIVATE_DEPENDENCIES
    "Should we fetch, build and link to private copies of libucl and libkqueue?"
    ON)

if (USE_PRIVATE_DEPENDENCIES)
    ExternalProject_Add(libucl
        GIT_REPOSITORY    https://github.com/vstakhov/libucl.git
        GIT_TAG           0.8.1
        INSTALL_COMMAND   ""
        )
endif()

CHECK_INCLUDE_FILES(sys/event.h HAVE_SYS_EVENT_H)

if(HAVE_SYS_EVENT_H)
elseif(USE_PRIVATE_DEPENDENCIES)
    ExternalProject_Add(libkqueue
        GIT_REPOSITORY    https://github.com/mheily/libkqueue.git
        GIT_TAG           v2.6.2
        INSTALL_COMMAND   ""
        )
endif()

add_subdirectory(src)
