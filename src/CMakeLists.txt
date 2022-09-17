set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS_DEBUG   "${CMAKE_C_FLAGS_DEBUG} -g3 -Wall -Wextra -Werror")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Wall -Wextra")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS_DEBUG} -g3 -Wall -Wextra -Werror")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Wall -Wextra")

check_include_files(sys/limits.h, HAVE_SYS_LIMITS_H)

add_executable(launchd
        calendar.c calendar.h
        cvec.h
        flopen.c
        keepalive.c keepalive.h
        channel.cc channel.h
        job.c job.h
        launchd.c
        log.c log.h
        manager.c manager.h
        manifest.c manifest.h
        options.cc options.h
        pidfile.c pidfile.h
        rpc_server.cc rpc_server.h
        socket.c socket.h
        timer.c timer.h
        uset.h
        util.h)

if (USE_PRIVATE_DEPENDENCIES)
    # TODO: Is there a better way to access the CMake variables in this external project?
    ExternalProject_Get_property(libucl LIBUCL_SOURCE_DIR)
    target_include_directories(launchd PRIVATE "${LIBUCL_SOURCE_DIR}/include")
    ExternalProject_Get_property(libucl LIBUCL_BINARY_DIR)
    target_link_libraries(launchd "${LIBUCL_BINARY_DIR}/libucl.a")
else()
    target_link_libraries(launchd "-lucl")
endif()

if(HAVE_SYS_EVENT_H)
else()
    if (USE_PRIVATE_DEPENDENCIES)
        # TODO: Is there a better way to access the CMake variables in this external project?
        ExternalProject_Get_property(libkqueue LIBKQUEUE_SOURCE_DIR)
        include_directories(launchd PRIVATE "${LIBKQUEUE_SOURCE_DIR}/include")
        ExternalProject_Get_property(libkqueue LIBKQUEUE_BINARY_DIR)
        target_link_libraries(launchd "${LIBKQUEUE_BINARY_DIR}/libkqueue.a")
    else()
        target_link_libraries(launchd "-lkqueue")
    endif()
    target_link_libraries(launchd "pthread")
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

configure_file(launchctl.in ${CMAKE_CURRENT_BINARY_DIR}/launchctl)

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/launchctl DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
install(CODE "FILE(MAKE_DIRECTORY \$ENV{DESTDIR}\/${PKGSTATEDIR})")
install(CODE "FILE(MAKE_DIRECTORY \$ENV{DESTDIR}\/${VARDIR}/run)")

#
# launchctl
#

add_executable(launchctl
        channel.h channel.cc
        launchctl.cc
        log.c log.h
        rpc_client.cc rpc_client.h
        util.h
        options.cc options.h)

if(HAVE_SYS_EVENT_H)
else()
    if (USE_PRIVATE_DEPENDENCIES)
        # TODO: Is there a better way to access the CMake variables in this external project?
        target_link_libraries(launchctl "${LIBKQUEUE_BINARY_DIR}/libkqueue.a")
    else()
        target_link_libraries(launchctl "-lkqueue")
    endif()
    target_link_libraries(launchctl "pthread")
endif()
