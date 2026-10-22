#include "engine/core/clock.hpp"

#include <chrono>

#include "engine/core/event.hpp"
#include "engine/core/panic.hpp"

namespace sg
{

auto SteadyClock::Now() const -> Millis
{
  const auto elapsed = std::chrono::steady_clock::now() - m_started;
  return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
}

auto FakeClock::Advance(Millis by) -> void
{
  SG_ASSERT(by >= 0, "FakeClock::Advance was given a negative step");
  m_now += by;
}

auto FakeClock::SetNow(Millis now) -> void
{
  SG_ASSERT(now >= m_now, "FakeClock::SetNow would move time backwards");
  m_now = now;
}

}  // namespace sg
