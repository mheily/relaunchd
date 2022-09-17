set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS_DEBUG   "-O0 -g3 -Wall -Wextra -Werror")
set(CMAKE_C_FLAGS_RELEASE "-O3 -Wall")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS_DEBUG   "-O0 -g3 -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -Wall -Wextra -Werror")

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

# TODO: Is there a better way to access the CMake variables in this external project?
ExternalProject_Get_property(libucl SOURCE_DIR)
target_include_directories(launchd PRIVATE "${SOURCE_DIR}/include")
ExternalProject_Get_property(libucl BINARY_DIR)
target_link_libraries(launchd "${BINARY_DIR}/libucl.a")

if(HAVE_SYS_EVENT_H)
else()
# TODO: Is there a better way to access the CMake variables in this external project?
ExternalProject_Get_property(libkqueue SOURCE_DIR)
target_include_directories(launchd PRIVATE "${SOURCE_DIR}/include")
ExternalProject_Get_property(libkqueue BINARY_DIR)
target_link_libraries(launchd "${BINARY_DIR}/libkqueue.a")
target_link_libraries(launchd "pthread")
endif()

# for asprintf() in glibc
add_compile_definitions(_GNU_SOURCE)

add_compile_definitions(PKGSTATEDIR="/var/db/relaunchd")

#
# launchctl
#



add_executable(launchctl launchctl.cc util.h)
