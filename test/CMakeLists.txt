set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g3 -O0")

add_executable(TestManifest manifest_test.cc ../src/manifest.cc)
target_link_libraries(TestManifest launch)
target_include_directories(TestManifest PRIVATE . ${CMAKE_CURRENT_SOURCE_DIR}/../src)
add_test(NAME TestManifest COMMAND TestManifest)
target_compile_definitions(TestManifest PRIVATE -DTESTDIR="${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(TestManager manager_test.cc ../src/manager.cc ${CMAKE_BINARY_DIR}/src/config.h)
target_link_libraries(TestManager launch)
target_include_directories(TestManager PRIVATE . ${CMAKE_CURRENT_SOURCE_DIR}/../src ${CMAKE_BINARY_DIR}/src)
add_test(NAME TestManager COMMAND TestManager)
target_compile_definitions(TestManager PRIVATE -DTESTDIR="${CMAKE_CURRENT_SOURCE_DIR}")
