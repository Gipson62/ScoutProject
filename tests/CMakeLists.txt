add_executable(sg_tests config/config_test.cpp
                       core/core_test.cpp
                       core/error_test.cpp
                       core/event_test.cpp
                       core/game_state_test.cpp
                       core/optional_ref_test.cpp
                       core/panic_test.cpp
                       core/scheduler_test.cpp
                       io/state_frame_test.cpp
                       io/web_server_test.cpp
                       log/event_log_test.cpp
                       options_test.cpp
                       script/script_host_test.cpp
                       toolchain_test.cpp)

target_link_libraries(sg_tests PRIVATE sg_engine sg_options GTest::gtest_main)

# The script tests load the real modes/capture.py, so they need to find it
# wherever the build directory happens to be.
target_compile_definitions(sg_tests PRIVATE SG_REPO_ROOT="${CMAKE_SOURCE_DIR}")

include(GoogleTest)
gtest_discover_tests(sg_tests)
