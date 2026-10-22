# The engine library every later module adds to. main.cpp and the sgengine
# executable arrive with milestone M9.
add_library(
  sg_engine STATIC
  config/config.cpp
  config/roster.cpp
  core/clock.cpp
  core/command_router.cpp
  core/core.cpp
  core/error.cpp
  core/event.cpp
  core/game_state.cpp
  core/json_impl.cpp
  core/mode.cpp
  core/panic.cpp
  core/random.cpp
  core/scheduler.cpp
  core/toolchain.cpp
  io/output_sink.cpp
  io/sim_transport.cpp
  io/web_server.cpp
  log/event_log.cpp
  script/script_host.cpp)

target_include_directories(sg_engine PUBLIC "${CMAKE_CURRENT_LIST_DIR}/..")

target_link_libraries(
  sg_engine
  PUBLIC sg_options Boost::headers tomlplusplus::tomlplusplus pocketpy
  PRIVATE Threads::Threads)
