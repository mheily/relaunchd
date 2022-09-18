# interface module for libkqueue's pkg-config file

set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH ON)

include(FindPkgConfig)
include(FindPackageHandleStandardArgs)
include(FeatureSummary)

pkg_check_modules(PKG_LibKQueue libkqueue)

if(PKG_LibKQueue_FOUND)
    set(LibKQueue_DEFINITIONS ${PKG_LibKQueue_CFLAGS_OTHER})
    set(LibKQueue_VERSION ${PKG_LibKQueue_VERSION})
    # NB: we could use ${LibKQueue_INCLUDE_DIR if libkqueue's .pc file set its 'includedir' 
    # directive to its actual include directory, instead of using the 'Cflags' directive!
    set(LibKQueue_INCLUDE_DIR ${PKG_LibKQueue_INCLUDE_DIRS})
    set(LibKQueue_LIBRARIES ${PKG_LibKQueue_LIBRARIES})
    if(PKG_LibKQueue_LIBRARY_DIRS)
        set(LibKQueue_LIBRARY_LDFLAGS "-L${PKG_LibKQueue_LIBRARY_DIRS}")
    else()
        set(LibKQueue_LIBRARY_LDFLAGS "")
    endif()
endif()

find_package_handle_standard_args(LibKQueue
    FOUND_VAR
        LibKQueue_FOUND
    REQUIRED_VARS
        LibKQueue_INCLUDE_DIR
    VERSION_VAR
        LibKQueue_VERSION
)

mark_as_advanced(LibKQueue_INCLUDE_DIR)

set_package_properties(LibKQueue PROPERTIES
                     DESCRIPTION "A library that emulates FreeBSD kqueue(2) on other platforms"
                     TYPE REQUIRED
                     URL "https://github.com/mheily/libkqueue")

