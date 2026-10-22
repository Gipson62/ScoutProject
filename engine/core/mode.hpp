#ifndef SG_ENGINE_CORE_MODE_HPP_
#define SG_ENGINE_CORE_MODE_HPP_

// The seam between the Core and the rules of a game.
//
// DESIGN.md has the Core call ScriptHost directly. One interface sits between
// them instead, for two reasons: the Core can be tested against a stub mode
// written in C++, before and independently of pocketpy, and the interpreter
// stays swappable in one place, which is what the design asks ScriptHost for.
// M6's ScriptHost is simply another implementation of Mode.

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "engine/core/event.hpp"
#include "engine/core/game_state.hpp"
#include "engine/core/scheduler.hpp"

namespace sg
{

// What a wristband resolved to. An empty team means a tag nobody enrolled; the
// mode decides what to do about it, usually a deny beep.
struct Tag
{
  std::string uid;
  std::string team;
  std::string player;
  std::string role;

  [[nodiscard]] auto Known() const -> bool
  {
    return !team.empty();
  }
};

class Roster
{
 public:
  Roster() = default;
  Roster(const Roster&) = default;
  Roster(Roster&&) = default;
  auto operator=(const Roster&) -> Roster& = default;
  auto operator=(Roster&&) -> Roster& = default;
  virtual ~Roster() = default;

  [[nodiscard]] virtual auto Lookup(std::string_view uid) const -> Tag = 0;
};

// The Core's face to a mode: the only way a mode reads time, schedules anything
// or produces an output. A mode that used the system clock or wrote to a socket
// directly would break the replay, and this interface is what makes that
// visible in a review.
class ModeHost
{
 public:
  ModeHost() = default;
  ModeHost(const ModeHost&) = default;
  ModeHost(ModeHost&&) = default;
  auto operator=(const ModeHost&) -> ModeHost& = default;
  auto operator=(ModeHost&&) -> ModeHost& = default;
  virtual ~ModeHost() = default;

  // game.now: the time of the event or timer being handled, not the wall clock.
  [[nodiscard]] virtual auto Now() const -> Millis = 0;

  // game.after and game.every, both relative to Now().
  [[nodiscard]] virtual auto After(Millis delay) -> TimerId = 0;
  [[nodiscard]] virtual auto Every(Millis interval) -> TimerId = 0;
  virtual auto Cancel(TimerId id) -> bool = 0;

  // Everything the mode wants done. Commands are collected and routed after the
  // hook returns.
  virtual auto Emit(Command command) -> void = 0;
};

// The rules of one game. Every hook is a no-op by default, so a mode overrides
// only what it uses.
class Mode
{
 public:
  Mode() = default;
  Mode(const Mode&) = default;
  Mode(Mode&&) = default;
  auto operator=(const Mode&) -> Mode& = default;
  auto operator=(Mode&&) -> Mode& = default;
  virtual ~Mode() = default;

  virtual auto OnStart(GameState& state, ModeHost& host) -> void;
  virtual auto OnTag(GameState& state, ModeHost& host, std::string_view point,
                     const Tag& tag) -> void;
  virtual auto OnButton(GameState& state, ModeHost& host,
                        std::string_view point) -> void;
  virtual auto OnAction(GameState& state, ModeHost& host, const Action& action)
      -> void;
  virtual auto OnAdmin(GameState& state, ModeHost& host, const Admin& admin)
      -> void;
  virtual auto OnTimer(GameState& state, ModeHost& host, TimerId id) -> void;
};

// A rebuild throws the mode away and loads it again from scratch, so the Core
// needs to be able to make a new one rather than reset the old one.
using ModeFactory = std::function<std::unique_ptr<Mode>()>;

}  // namespace sg

#endif  // SG_ENGINE_CORE_MODE_HPP_
