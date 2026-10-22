# The engine library every later module adds to. main.cpp and the sgengine
# executable arrive with milestone M9.
add_library(
  sg_engine STATIC
  core/clock.cpp
  core/error.cpp
  core/event.cpp
  core/json_impl.cpp
  core/panic.cpp
  core/scheduler.cpp
  core/toolchain.cpp
  log/event_log.cpp)

target_include_directories(sg_engine PUBLIC "${CMAKE_CURRENT_LIST_DIR}/..")

target_link_libraries(
  sg_engine
  PUBLIC sg_options Boost::headers tomlplusplus::tomlplusplus pocketpy
  PRIVATE Threads::Threads)
