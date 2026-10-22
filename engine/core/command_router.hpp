#ifndef SG_ENGINE_CORE_COMMAND_ROUTER_HPP_
#define SG_ENGINE_CORE_COMMAND_ROUTER_HPP_

// The only way out of the Core. Every LED, beep, sound, map pixel, panel push
// and announcement the game produces passes through here, which is what lets a
// replay be silent: mute the router and the same hooks run with no side effect
// anyone can see.

#include "engine/core/event.hpp"
#include "engine/core/game_state.hpp"

namespace sg
{

// What the adapters implement: the transport for a point's LED and buzzer, the
// station for map pixels, audio for sounds, the web server for panels and
// announcements. Phase 1 has one sink, the simulator; Phase 3 fans out by
// command type inside its own sink.
class CommandSink
{
 public:
  CommandSink() = default;
  CommandSink(const CommandSink&) = default;
  CommandSink(CommandSink&&) = default;
  auto operator=(const CommandSink&) -> CommandSink& = default;
  auto operator=(CommandSink&&) -> CommandSink& = default;
  virtual ~CommandSink() = default;

  virtual auto Send(const Command& command) -> void = 0;
};

class CommandRouter
{
 public:
  explicit CommandRouter(CommandSink& sink) : m_sink(sink) {}

  auto Send(const Command& command) -> void;

  // Muting is not refcounted: Rebuild mutes once and unmutes once, and a nested
  // rebuild cannot happen on a single thread.
  auto Mute() -> void
  {
    m_muted = true;
  }
  auto Unmute() -> void
  {
    m_muted = false;
  }
  [[nodiscard]] auto Muted() const -> bool
  {
    return m_muted;
  }

  // One resync of everything a replay did not send: the current LED of every
  // point, in point order. Called once after a rebuild, with the router
  // unmuted.
  auto PushFullState(const GameState& state) -> void;

 private:
  CommandSink& m_sink;
  bool m_muted = false;
};

}  // namespace sg

#endif  // SG_ENGINE_CORE_COMMAND_ROUTER_HPP_
