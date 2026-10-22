#include "engine/core/mode.hpp"

#include <string_view>

#include "engine/core/event.hpp"
#include "engine/core/game_state.hpp"
#include "engine/core/scheduler.hpp"

namespace sg
{

// Every hook does nothing unless a mode overrides it. The arguments are named
// and discarded rather than left unnamed, so a reader sees what an override
// would receive.

auto Mode::OnStart(GameState& state, ModeHost& host) -> void
{
  static_cast<void>(state);
  static_cast<void>(host);
}

auto Mode::OnTag(GameState& state, ModeHost& host, std::string_view point,
                 const Tag& tag) -> void
{
  static_cast<void>(state);
  static_cast<void>(host);
  static_cast<void>(point);
  static_cast<void>(tag);
}

auto Mode::OnButton(GameState& state, ModeHost& host, std::string_view point)
    -> void
{
  static_cast<void>(state);
  static_cast<void>(host);
  static_cast<void>(point);
}

auto Mode::OnAction(GameState& state, ModeHost& host, const Action& action)
    -> void
{
  static_cast<void>(state);
  static_cast<void>(host);
  static_cast<void>(action);
}

auto Mode::OnAdmin(GameState& state, ModeHost& host, const Admin& admin) -> void
{
  static_cast<void>(state);
  static_cast<void>(host);
  static_cast<void>(admin);
}

auto Mode::OnTimer(GameState& state, ModeHost& host, TimerId id) -> void
{
  static_cast<void>(state);
  static_cast<void>(host);
  static_cast<void>(id);
}

}  // namespace sg
