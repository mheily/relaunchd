set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3 -O0")

add_executable(TestManifest manifest_test.cc ../src/manifest.cc)
target_link_libraries(TestManifest PRIVATE launch nlohmann_json::nlohmann_json)
target_include_directories(TestManifest PRIVATE . ${CMAKE_CURRENT_SOURCE_DIR}/../src)
add_test(NAME TestManifest COMMAND TestManifest)
target_compile_definitions(TestManifest PRIVATE -DTESTDIR="${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(TestManager manager_test.cc)
#         ../src/manager.cc
#         ../src/channel.cc ../src/state_file.cc ../src/job.cc ../src/options.cc ../src/rpc_server.cc ../src/calendar.cc ../src/manifest.cc ../src/dependency.cc ../src/domain.cc ../src/log.cc
#        ${CMAKE_BINARY_DIR}/src/config.h)
target_link_libraries(TestManager PRIVATE launch nlohmann_json::nlohmann_json)
target_include_directories(TestManager PRIVATE . ${CMAKE_CURRENT_SOURCE_DIR}/../src ${CMAKE_BINARY_DIR}/src)
add_test(NAME TestManager COMMAND TestManager)
target_compile_definitions(TestManager PRIVATE -DTESTDIR="${CMAKE_CURRENT_SOURCE_DIR}")

#
# Code coverage report
#
if(ENABLE_COVERAGE)
    setup_target_for_coverage_lcov(
            NAME TestManager_coverage
            EXECUTABLE TestManager
            DEPENDENCIES TestManager
            EXCLUDE '${CMAKE_SOURCE_DIR}/vendor/*' '${CMAKE_SOURCE_DIR}/test/*' '/opt/homebrew/opt/llvm/include/c++/*'
    )
endif()
