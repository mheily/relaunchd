cmake_minimum_required(VERSION 3.13)
project(relaunchd)

# add our own module repository to CMake's search path:
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules ${CMAKE_MODULE_PATH} )

include(ExternalProject)
include(CheckIncludeFiles)
include(GNUInstallDirs)

option(USE_PRIVATE_DEPENDENCIES
    "Should we fetch, build and link to private copies of libucl and libkqueue?"
    ON)

if (USE_PRIVATE_DEPENDENCIES)
    ExternalProject_Add(libucl
        GIT_REPOSITORY    https://github.com/vstakhov/libucl.git
        GIT_TAG           0.8.1
        INSTALL_COMMAND   ""
        )
else()
    find_package(LibUCL 0.8.1 REQUIRED)
endif()

CHECK_INCLUDE_FILES(sys/event.h HAVE_SYS_EVENT_H)

if(HAVE_SYS_EVENT_H)
else()
    if(USE_PRIVATE_DEPENDENCIES)
        ExternalProject_Add(libkqueue
            GIT_REPOSITORY    https://github.com/mheily/libkqueue.git
            GIT_TAG           v2.6.2
            INSTALL_COMMAND   ""
            )
    else()
        find_package(LibKQueue 2.6.2 REQUIRED)
    endif()
endif()

add_subdirectory(src)
