set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS_DEBUG   "${CMAKE_C_FLAGS_DEBUG} -g3 -Wall -Wextra -Werror")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Wall -Wextra")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS_DEBUG} -g3 -Wall -Wextra -Werror")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Wall -Wextra")

check_include_files(sys/limits.h, HAVE_SYS_LIMITS_H)

add_library(launch STATIC 
        channel.h channel.cc
        log.cc log.h
        util.h
        options.cc options.h
        signal.cc signal.h
        state_file.cc state_file.hpp
        )

if (CMAKE_SYSTEM_NAME MATCHES Linux)
    # this makes adding libthread explicitly (below) redundant
    # but adding just that library is not necessarily sufficient
    # (go figure why...)
    target_link_options(launch PUBLIC "-pthread")
endif()

add_executable(launchd
        calendar.cc calendar.h
        channel.h
        cvec.h
        flopen.c
        keepalive.cc keepalive.h
        job.cc job.h
        launchd.cc
        log.h
        manager.cc manager.h
        manifest.cc manifest.h
        options.h
        rpc_server.cc rpc_server.h
        socket.cc socket.h
        timer.cc timer.h
        uset.h
        util.h)
target_link_libraries(launchd launch pthread)

if (USE_PRIVATE_DEPENDENCIES)
    # TODO: Is there a better way to access the CMake variables in this external project?
    # Answer: yes, you know where they get checked out w.r.t. the build dir (CMAKE_BINARY_DIR)
    ExternalProject_Get_property(libucl SOURCE_DIR)
    # libucl is used only by launchd so add its header repository only to the launchd target
    target_include_directories(launchd PRIVATE "${SOURCE_DIR}/include")
    ExternalProject_Get_property(libucl BINARY_DIR)
    target_link_libraries(launchd "${BINARY_DIR}/libucl.a")
else()
    target_link_options(launchd PRIVATE "${LibUCL_LIBRARY_LDFLAGS}")
    target_link_libraries(launchd "${LibUCL_LIBRARIES}")
endif()

if(HAVE_SYS_EVENT_H)
else()
    target_link_libraries(launchd "${LibKQueue_LIBRARIES}")
    # add the libkqueue header and library directories to all targets in this source directory:
    include_directories("${LibKQueue_INCLUDE_DIR}")
    add_link_options("${LibKQueue_LIBRARY_LDFLAGS}")
endif()

# for asprintf() in glibc
add_compile_definitions(_GNU_SOURCE)

if ((CMAKE_INSTALL_PREFIX MATCHES "^/usr$") OR (CMAKE_INSTALL_PREFIX MATCHES "^/usr/local$"))
    set(VARDIR "/var")
    set(SYSCONFDIR "/etc")
else()
    set(VARDIR "${CMAKE_INSTALL_PREFIX}/var")
    set(SYSCONFDIR "${CMAKE_INSTALL_PREFIX}/etc")
endif()
set(PKGSTATEDIR "${VARDIR}/db/relaunchd")

configure_file(config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)
include_directories("${CMAKE_CURRENT_BINARY_DIR}")

install(TARGETS launchd DESTINATION ${CMAKE_INSTALL_PREFIX}/sbin)

configure_file(launchctl.in ${CMAKE_CURRENT_BINARY_DIR}/launchctl.sh)

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/launchctl.sh DESTINATION ${CMAKE_INSTALL_PREFIX}/bin RENAME launchctl)
install(CODE "FILE(MAKE_DIRECTORY \$ENV{DESTDIR}\/${PKGSTATEDIR})")
install(CODE "FILE(MAKE_DIRECTORY \$ENV{DESTDIR}\/${VARDIR}/run)")

#
# launchctl
#

add_executable(launchctl
        channel.h
        launchctl.cc
        log.h
        util.h
        options.h)
target_link_libraries(launchctl launch pthread)

if(HAVE_SYS_EVENT_H)
else()
    target_link_libraries(launchctl "${LibKQueue_LIBRARIES}")
endif()
