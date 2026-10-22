#ifndef SG_ENGINE_CORE_CORE_HPP_
#define SG_ENGINE_CORE_CORE_HPP_

// The engine loop. Everything that changes the game passes through OnInput, and
// nothing changes the game that is not in the log first.

#include <cstdint>
#include <memory>
#include <vector>

#include "engine/core/clock.hpp"
#include "engine/core/command_router.hpp"
#include "engine/core/error.hpp"
#include "engine/core/event.hpp"
#include "engine/core/game_state.hpp"
#include "engine/core/mode.hpp"
#include "engine/core/scheduler.hpp"
#include "engine/log/event_log.hpp"

namespace sg
{

class Core final : public ModeHost
{
 public:
  Core(Clock& clock, EventLog& log, CommandRouter& router, const Roster& roster,
       Setup setup, ModeFactory factory);

  // Loads the mode, runs its start hook and replays whatever the log already
  // holds, so a restart resumes the game instead of beginning a new one. This
  // is Rebuild: starting and recovering are the same operation.
  auto Start() -> void
  {
    Rebuild();
  }

  // One input, in the order the design fixes: durable first, then every timer
  // due before it, then the hook.
  //
  // An event whose time is already in the past, and any Void, cannot be applied
  // forward — a timer that should have fired before it has already fired — so
  // the state is thrown away and recomputed from the log.
  [[nodiscard]] auto OnInput(Event event) -> Result<void>;

  // Fires timers that fell due between inputs. The 100 ms Asio timer calls it.
  auto Tick() -> void
  {
    RunUntil(m_clock.Now());
  }

  auto RunUntil(Millis t) -> void;
  auto Rebuild() -> void;

  [[nodiscard]] auto State() const -> const GameState&
  {
    return m_state;
  }
  [[nodiscard]] auto Timers() const -> const Scheduler&
  {
    return m_scheduler;
  }
  // How many rebuilds have happened: the dashboard shows it, and the tests
  // check that a live run did none.
  [[nodiscard]] auto Rebuilds() const -> std::uint64_t
  {
    return m_rebuilds;
  }

  // ModeHost
  [[nodiscard]] auto Now() const -> Millis override
  {
    return m_now;
  }
  [[nodiscard]] auto After(Millis delay) -> TimerId override;
  [[nodiscard]] auto Every(Millis interval) -> TimerId override;
  auto Cancel(TimerId id) -> bool override;
  auto Emit(Command command) -> void override;

 private:
  auto Dispatch(const Event& event) -> void;
  auto FlushCommands() -> void;

  Clock& m_clock;
  EventLog& m_log;
  CommandRouter& m_router;
  const Roster& m_roster;
  Setup m_setup;
  ModeFactory m_factory;

  std::unique_ptr<Mode> m_mode;
  GameState m_state;
  Scheduler m_scheduler;
  std::vector<Command> m_pending;
  // The time the mode sees: the time of the event or timer being handled.
  Millis m_now = 0;
  std::uint64_t m_rebuilds = 0;
};

}  // namespace sg

#endif  // SG_ENGINE_CORE_CORE_HPP_
