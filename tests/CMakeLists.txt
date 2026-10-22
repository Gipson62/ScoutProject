add_executable(sg_tests toolchain_test.cpp)

target_link_libraries(sg_tests PRIVATE sg_engine sg_options GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(sg_tests)
