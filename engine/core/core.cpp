#include "engine/core/core.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "engine/core/error.hpp"
#include "engine/core/event.hpp"
#include "engine/core/game_state.hpp"
#include "engine/core/mode.hpp"
#include "engine/core/optional_ref.hpp"
#include "engine/core/panic.hpp"
#include "engine/core/scheduler.hpp"

namespace sg
{

Core::Core(Clock& clock, EventLog& log, CommandRouter& router,
           const Roster& roster, Setup setup, ModeFactory factory)
    : m_clock(clock),
      m_log(log),
      m_router(router),
      m_roster(roster),
      m_setup(std::move(setup)),
      m_factory(std::move(factory)),
      m_mode(m_factory()),
      m_state(GameState::Initial(m_setup))
{
  SG_ASSERT(m_mode != nullptr, "the mode factory returned nothing");
}

auto Core::After(Millis delay) -> TimerId
{
  return m_scheduler.After(m_now + delay);
}

auto Core::Every(Millis interval) -> TimerId
{
  return m_scheduler.Every(m_now + interval, interval);
}

auto Core::Cancel(TimerId id) -> bool
{
  return m_scheduler.Cancel(id);
}

auto Core::Emit(Command command) -> void
{
  m_pending.push_back(std::move(command));
}

auto Core::FlushCommands() -> void
{
  // Taken by value first: routing a command can reach a sink that feeds another
  // input back in, and that input must not append to the queue being drained.
  std::vector<Command> commands;
  commands.swap(m_pending);

  for (Command& command : commands)
  {
    if (LedState* led = std::get_if<LedState>(&command))
    {
      // The mode says colour and pattern; the engine owns the version. A node
      // reports the version it is showing in its heartbeat, so a mismatch tells
      // the engine to resend rather than let a point drift.
      if (OptionalRef<PointState> point = m_state.FindPoint(led->point))
      {
        led->version = static_cast<std::uint16_t>(point->led.version + 1);
        point->led = *led;
      }
    }
    m_router.Send(command);
  }
}

auto Core::RunUntil(Millis t) -> void
{
  while (const std::optional<Timer> timer = m_scheduler.PopDue(t))
  {
    // The mode sees the timer's own due time, not the wall clock: a timer that
    // fires during a catch-up must compute the same thing it would have
    // computed on time.
    m_now = timer->due;
    m_mode->OnTimer(m_state, *this, timer->id);
    FlushCommands();
  }
  m_now = std::max(m_now, t);
}

auto Core::Dispatch(const Event& event) -> void
{
  std::visit(
      [this](const auto& data)
      {
        using T = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<T, Tap>)
        {
          m_mode->OnTag(m_state, *this, data.point, m_roster.Lookup(data.uid));
        }
        else if constexpr (std::is_same_v<T, Button>)
        {
          m_mode->OnButton(m_state, *this, data.point);
        }
        else if constexpr (std::is_same_v<T, Action>)
        {
          m_mode->OnAction(m_state, *this, data);
        }
        else if constexpr (std::is_same_v<T, Admin>)
        {
          m_mode->OnAdmin(m_state, *this, data);
        }
        else
        {
          // A Void has no hook: its whole effect is that Rebuild leaves the
          // event it targets out of the replay.
          static_assert(std::is_same_v<T, Void>);
        }
      },
      event.data);
  FlushCommands();
}

auto Core::OnInput(Event event) -> Result<void>
{
  const Result<std::uint64_t> seq = m_log.Append(event);
  if (!seq)
  {
    return std::unexpected(seq.error());
  }
  event.seq = *seq;

  if (event.t < m_now || std::holds_alternative<Void>(event.data))
  {
    Rebuild();
    return {};
  }

  RunUntil(event.t);
  Dispatch(event);
  return {};
}

auto Core::Rebuild() -> void
{
  m_router.Mute();

  m_mode = m_factory();
  SG_ASSERT(m_mode != nullptr, "the mode factory returned nothing");
  m_state = GameState::Initial(m_setup);
  m_scheduler.Clear();
  m_pending.clear();
  m_now = 0;

  m_mode->OnStart(m_state, *this);
  FlushCommands();

  for (const Event& event : m_log.ByTimeWithoutVoided())
  {
    RunUntil(event.t);
    Dispatch(event);
  }

  // Catch up to the present, so timers that fell due during the replay's own
  // tail fire before anyone looks at the state.
  RunUntil(m_clock.Now());

  m_router.Unmute();
  m_router.PushFullState(m_state);
  ++m_rebuilds;
}

}  // namespace sg
