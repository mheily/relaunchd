# interface module for libucl's pkg-config file

set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH ON)

include(FindPkgConfig)
include(FindPackageHandleStandardArgs)
include(FeatureSummary)

pkg_check_modules(PKG_LibUCL libucl)

if(PKG_LibUCL_FOUND)
    set(LibUCL_DEFINITIONS ${PKG_LibUCL_CFLAGS_OTHER})
    set(LibUCL_VERSION ${PKG_LibUCL_VERSION})
    # NB: we could use ${LibUCL_INCLUDE_DIR if libucl's .pc file set its 'includedir' 
    # directive to its actual include directory, instead of using the 'Cflags' directive!
    set(LibUCL_INCLUDE_DIR ${PKG_LibUCL_INCLUDE_DIRS})
    set(LibUCL_LIBRARIES ${PKG_LibUCL_LIBRARIES})
    if(PKG_LibUCL_LIBRARY_DIRS)
        set(LibUCL_LIBRARY_LDFLAGS "-L${PKG_LibUCL_LIBRARY_DIRS}")
    else()
        set(LibUCL_LIBRARY_LDFLAGS "")
    endif()
endif()

find_package_handle_standard_args(LibUCL
    FOUND_VAR
        LibUCL_FOUND
    REQUIRED_VARS
        LibUCL_INCLUDE_DIR
    VERSION_VAR
        LibUCL_VERSION
)

mark_as_advanced(LibUCL_INCLUDE_DIR)

set_package_properties(LibUCL PROPERTIES
                     DESCRIPTION "A Universal Configuration Library"
                     TYPE REQUIRED
                     URL "https://github.com/vstakhov/libucl")

