set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS_DEBUG   "${CMAKE_C_FLAGS_DEBUG} -g3 -O0 -Wall -Wextra")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Wall -Wextra -Werror")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS_DEBUG} -g3 -O0 -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Os -Wall -Wextra -Werror")

check_include_files(sys/limits.h, HAVE_SYS_LIMITS_H)

set(LAUNCH_SRC
        channel.h channel.cc
        domain.h domain.cc
        event.h
        exec_monitor.h
        job.cc job.h
        log.cc log.h
        main.cc
        manager.cc manager.h
        manifest.cc manifest.h
        options.cc options.h
        rpc_client.cc rpc_client.h
        rpc_server.cc rpc_server.h
        signal_names.h
        state_file.cc state_file.hpp
        )
if (USE_PRIVATE_DEPENDENCIES)
    set(LAUNCH_SRC ${LAUNCH_SRC})
    if (USE_TINYXML)
        set(LAUNCH_SRC ${LAUNCH_SRC} ../vendor/tinyxml2.cpp ../vendor/tinyxml2.h)
    endif ()
endif ()
add_library(launch INTERFACE)
target_sources(launch INTERFACE ${LAUNCH_SRC})
target_link_libraries(launch INTERFACE nlohmann_json::nlohmann_json)
if (USE_EXTERNAL_CXX17_FILESYSTEM)
    target_link_libraries(launch INTERFACE stdc++fs)
endif ()

if (NOT USE_PRIVATE_DEPENDENCIES)
    # we register libtinyxml2 as a public dependency of liblaunch
    # instead of including the tinyxml2 sources in it; also
    # add the tinyxml2 include directory to all targets in the current dir.
    target_link_libraries(launch INTERFACE tinyxml2::tinyxml2)
    include_directories(tinyxml2::tinyxml2)
endif()

add_executable(launchd launchd.cc launchctl.cc ${LAUNCH_SRC})

target_link_libraries(launchd PRIVATE nlohmann_json::nlohmann_json)

# DISABLED: this symbol exists in glibc but warns that it will always fail
# need to actually try compiling a program and using the function.
#check_symbol_exists(setlogin "unistd.h" HAVE_SETLOGIN)
#if(HAVE_SETLOGIN)
#    target_compile_definitions(launchd PRIVATE -DHAVE_SETLOGIN)
#endif()

if(USE_LIBKQUEUE)
    target_link_libraries(launchd "${LibKQueue_LIBRARIES} pthread")
    # add the libkqueue header and library directories to all targets in this source directory:
    include_directories("${LibKQueue_INCLUDE_DIR}")
    add_link_options("${LibKQueue_LIBRARY_LDFLAGS}")
endif()

# for asprintf() in glibc
add_compile_definitions(_GNU_SOURCE)

if ((CMAKE_INSTALL_PREFIX MATCHES "^/(usr)?(/local)?$"))
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

install(CODE "FILE(MAKE_DIRECTORY \$ENV{DESTDIR}\/${PKGSTATEDIR})")
install(CODE "FILE(MAKE_DIRECTORY \$ENV{DESTDIR}\/${VARDIR}/run)")

if(ENABLE_LTO AND ipo_supported)
    message(STATUS "Enabling link-time optimization")
    set_property(TARGET launchd PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
    message(STATUS "LTO not supported: <${ipo_error}>")
endif()

include(EnableSanitizers)
