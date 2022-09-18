set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS_DEBUG   "${CMAKE_C_FLAGS_DEBUG} -g3 -Wall -Wextra -Werror")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Wall -Wextra")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS_DEBUG} -g3 -Wall -Wextra -Werror")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Wall -Wextra")

check_include_files(sys/limits.h, HAVE_SYS_LIMITS_H)

add_library(launch STATIC 
        channel.h channel.cc
        log.c log.h
        util.h
        options.cc options.h)

add_executable(launchd
        calendar.c calendar.h
        channel.h
        cvec.h
        flopen.c
        keepalive.c keepalive.h
        job.c job.h
        launchd.c
        log.h
        manager.cc manager.h
        manifest.c manifest.h
        options.h
        pidfile.c pidfile.h
        rpc_server.cc rpc_server.h
        socket.c socket.h
        timer.c timer.h
        uset.h
        util.h)
target_link_libraries(launchd launch pthread)

if (USE_PRIVATE_DEPENDENCIES)
    # TODO: Is there a better way to access the CMake variables in this external project?
    ExternalProject_Get_property(libucl SOURCE_DIR)
    include_directories(launchd "${SOURCE_DIR}/include")
    ExternalProject_Get_property(libucl BINARY_DIR)
    target_link_libraries(launchd "${BINARY_DIR}/libucl.a")
else()
    target_link_libraries(launchd "-lucl")
endif()

if(HAVE_SYS_EVENT_H)
else()
    if (USE_PRIVATE_DEPENDENCIES)
        # TODO: Is there a better way to access the CMake variables in this external project?
        ExternalProject_Get_property(libkqueue SOURCE_DIR)
        set(LIBKQUEUE_SOURCE_DIR ${SOURCE_DIR})
        include_directories(launchd "${LIBKQUEUE_SOURCE_DIR}/include")
        ExternalProject_Get_property(libkqueue BINARY_DIR)
        set(LIBKQUEUE_BINARY_DIR ${BINARY_DIR})
        target_link_libraries(launchd "${LIBKQUEUE_BINARY_DIR}/libkqueue.a")
    else()
        target_link_libraries(launchd "-lkqueue")
        include_directories(launchd "-I/usr/include/kqueue") # TODO: can we get this from pkgconfig instead?
    endif()
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
        rpc_client.cc rpc_client.h
        util.h
        options.h)
target_link_libraries(launchctl launch pthread)

if(HAVE_SYS_EVENT_H)
else()
    if (USE_PRIVATE_DEPENDENCIES)
        # TODO: Is there a better way to access the CMake variables in this external project?
        target_link_libraries(launchctl "${LIBKQUEUE_BINARY_DIR}/libkqueue.a")
    else()
        target_link_libraries(launchctl "-lkqueue")
    endif()
endif()
