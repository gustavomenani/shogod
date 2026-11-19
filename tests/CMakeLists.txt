set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_SOURCE_DIR}/vendor/googletest" "${CMAKE_BINARY_DIR}/googletest" EXCLUDE_FROM_ALL)

add_executable(shogod_tests
    test_main.cpp
    test_math.cpp
    test_pattern.cpp
)
target_link_libraries(shogod_tests PRIVATE shogod_core GTest::gtest_main)
target_include_directories(shogod_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(shogod_tests PROPERTIES
    OUTPUT_NAME "shogod_tests"
    MSVC_RUNTIME_LIBRARY "MultiThreaded"
)
target_compile_options(shogod_tests PRIVATE /W4 /permissive- /utf-8 /MP)

include(GoogleTest)
gtest_discover_tests(shogod_tests)
