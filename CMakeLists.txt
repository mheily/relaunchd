cmake_minimum_required(VERSION 3.13)
project(relaunchd)

# add our own module repository to CMake's search path:
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules ${CMAKE_MODULE_PATH} )

include(ExternalProject)
include(CheckIncludeFiles)
include(GNUInstallDirs)

option(USE_PRIVATE_DEPENDENCIES
    "Should we fetch, build and link to a private copy of libkqueue?"
    ON)

CHECK_INCLUDE_FILES(sys/event.h HAVE_SYS_EVENT_H)

if(HAVE_SYS_EVENT_H)
else()
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

add_subdirectory(src)
