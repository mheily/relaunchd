cmake_minimum_required(VERSION 3.10)
project(relaunchd C)

include(ExternalProject)
include(CheckIncludeFiles)

ExternalProject_Add(libucl
        GIT_REPOSITORY    https://github.com/vstakhov/libucl.git
        GIT_TAG           0.8.1
        INSTALL_COMMAND   ""
        )

CHECK_INCLUDE_FILES(sys/event.h HAVE_SYS_EVENT_H)

if(HAVE_SYS_EVENT_H)
else()
ExternalProject_Add(libkqueue
        GIT_REPOSITORY    https://github.com/mheily/libkqueue.git
        GIT_TAG           v2.6.2
        INSTALL_COMMAND   ""
        )
endif()

add_subdirectory(src)
