set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS_DEBUG   "-O0 -g3 -Wall -Wextra -Werror")
set(CMAKE_C_FLAGS_RELEASE "-O3 -Wall")

INCLUDE (CheckIncludeFiles)

CHECK_INCLUDE_FILES(sys/limits.h, HAVE_SYS_LIMITS_H)

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

add_compile_definitions(PKGSTATEDIR="/var/db/relaunchd")
