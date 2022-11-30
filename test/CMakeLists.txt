set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3 -O0")

add_executable(test_all main_test.cc manager_test.cc manifest_test.cc state_file_test.cc common.hpp)
target_link_libraries(test_all PRIVATE launch nlohmann_json::nlohmann_json)
target_include_directories(test_all PRIVATE . ${CMAKE_CURRENT_SOURCE_DIR}/../src ${CMAKE_BINARY_DIR}/src)
add_test(NAME test_all COMMAND test_all -v)
target_compile_definitions(test_all PRIVATE
        -DTESTDIR="${CMAKE_CURRENT_SOURCE_DIR}"
        -DTMPDIR="${CMAKE_BINARY_DIR}/Testing/Temporary"
        )

#
# Code coverage report
#
if(ENABLE_COVERAGE)
    setup_target_for_coverage_lcov(
            NAME test_all_coverage
            EXECUTABLE test_all
            DEPENDENCIES test_all
            EXCLUDE '${CMAKE_SOURCE_DIR}/vendor/*' '${CMAKE_SOURCE_DIR}/test/*' '/opt/homebrew/opt/llvm/include/c++/*'
    )
endif()
