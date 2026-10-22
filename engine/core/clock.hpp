#ifndef SG_ENGINE_CORE_CLOCK_HPP_
#define SG_ENGINE_CORE_CLOCK_HPP_

// Game code never reads the system clock. It reads a Clock, so a replay can be
// driven at any speed and a test can jump five minutes without waiting.

#include <chrono>

#include "engine/core/event.hpp"

namespace sg
{

class Clock
{
 public:
  Clock() = default;
  Clock(const Clock&) = default;
  Clock(Clock&&) = default;
  auto operator=(const Clock&) -> Clock& = default;
  auto operator=(Clock&&) -> Clock& = default;
  virtual ~Clock() = default;

  // Milliseconds since the session started. Monotone: it never goes backwards,
  // so a timer due time computed from it stays due.
  [[nodiscard]] virtual auto Now() const -> Millis = 0;
};

// What the engine runs on: std::chrono::steady_clock, with its zero at
// construction. Not system_clock, which an NTP step or a time-zone change can
// move backwards in the middle of a game.
class SteadyClock final : public Clock
{
 public:
  SteadyClock() : m_started(std::chrono::steady_clock::now()) {}

  [[nodiscard]] auto Now() const -> Millis override;

 private:
  std::chrono::steady_clock::time_point m_started;
};

// What the tests run on, and what a fast-forward replay would use.
class FakeClock final : public Clock
{
 public:
  explicit FakeClock(Millis now = 0) : m_now(now) {}

  [[nodiscard]] auto Now() const -> Millis override
  {
    return m_now;
  }

  // Moves time forward. A negative step would break the monotony every due
  // time depends on, so it panics instead of silently going back.
  auto Advance(Millis by) -> void;
  auto SetNow(Millis now) -> void;

 private:
  Millis m_now = 0;
};

}  // namespace sg

#endif  // SG_ENGINE_CORE_CLOCK_HPP_
