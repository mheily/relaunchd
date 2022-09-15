set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS_DEBUG   "${CMAKE_C_FLAGS_DEBUG} -g3 -Wall -Wextra -Werror")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Wall -Wextra")

check_include_files(sys/limits.h, HAVE_SYS_LIMITS_H)

add_executable(launchd
        calendar.c calendar.h
        cvec.h
        flopen.c
        keepalive.c keepalive.h
        job.c job.h
        launchd.c
        log.c log.h
        manager.c manager.h
        manifest.c manifest.h
        options.h
        pidfile.c pidfile.h
        socket.c socket.h
        timer.c timer.h
        uset.h
        util.h
        )

if (USE_PRIVATE_DEPENDENCIES)
    # TODO: Is there a better way to access the CMake variables in this external project?
    ExternalProject_Get_property(libucl SOURCE_DIR)
    target_include_directories(launchd PRIVATE "${SOURCE_DIR}/include")
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
        target_include_directories(launchd PRIVATE "${SOURCE_DIR}/include")
        ExternalProject_Get_property(libkqueue BINARY_DIR)
        target_link_libraries(launchd "${BINARY_DIR}/libkqueue.a")
    else()
        target_link_libraries(launchd "-lkqueue")
    endif()
    target_link_libraries(launchd "pthread")
endif()

# for asprintf() in glibc
add_compile_definitions(_GNU_SOURCE)

set(PKGSTATEDIR "${CMAKE_INSTALL_PREFIX}/var/db/relaunchd")

configure_file(config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)
include_directories("${CMAKE_CURRENT_BINARY_DIR}")

install(TARGETS launchd DESTINATION ${CMAKE_INSTALL_PREFIX}/sbin)
INSTALL(CODE "FILE(MAKE_DIRECTORY \$ENV{DESTDIR}\/${PKGSTATEDIR})")
INSTALL(CODE "FILE(MAKE_DIRECTORY \$ENV{DESTDIR}\/${CMAKE_INSTALL_PREFIX}/var/run)")

#
# launchctl
#

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS_DEBUG} -g3 -Wall -Wextra -Werror")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Wall -Wextra")

add_executable(launchctl launchctl.cc util.h)
