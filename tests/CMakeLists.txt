add_executable(sg_tests core/error_test.cpp
                       core/event_test.cpp
                       core/game_state_test.cpp
                       core/optional_ref_test.cpp
                       core/panic_test.cpp
                       core/scheduler_test.cpp
                       log/event_log_test.cpp
                       toolchain_test.cpp)

target_link_libraries(sg_tests PRIVATE sg_engine sg_options GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(sg_tests)
