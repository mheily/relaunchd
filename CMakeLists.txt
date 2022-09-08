cmake_minimum_required(VERSION 3.10)
project(relaunchd C)

include(ExternalProject)

ExternalProject_Add(libucl
        GIT_REPOSITORY    https://github.com/vstakhov/libucl.git
        GIT_TAG           0.8.1
        INSTALL_COMMAND   ""
        )

#ExternalProject_Add(libucl
#        URL file://${CMAKE_CURRENT_SOURCE_DIR}/vendor/libucl-0.7.3.tar.gz)

add_subdirectory(src)