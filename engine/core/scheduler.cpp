#include "engine/core/scheduler.hpp"

#include <optional>
#include <utility>

#include "engine/core/event.hpp"
#include "engine/core/panic.hpp"

namespace sg
{

auto Scheduler::After(Millis due) -> TimerId
{
  const TimerId id = m_next_id++;
  m_timers.emplace(id, Timer{due, 0, id});
  m_queue.emplace(due, id);
  return id;
}

auto Scheduler::Every(Millis due, Millis interval) -> TimerId
{
  SG_ASSERT(interval > 0, "Scheduler::Every needs a positive interval");
  const TimerId id = m_next_id++;
  m_timers.emplace(id, Timer{due, interval, id});
  m_queue.emplace(due, id);
  return id;
}

auto Scheduler::Cancel(TimerId id) -> bool
{
  const auto found = m_timers.find(id);
  if (found == m_timers.end())
  {
    return false;
  }
  m_queue.erase(std::pair{found->second.due, id});
  m_timers.erase(found);
  return true;
}

auto Scheduler::NextDue() const -> std::optional<Millis>
{
  if (m_queue.empty())
  {
    return std::nullopt;
  }
  return m_queue.begin()->first;
}

auto Scheduler::PopDue(Millis t) -> std::optional<Timer>
{
  if (m_queue.empty() || m_queue.begin()->first > t)
  {
    return std::nullopt;
  }

  const auto next = m_queue.begin();
  const TimerId id = next->second;
  m_queue.erase(next);

  const auto found = m_timers.find(id);
  SG_ASSERT(found != m_timers.end(), "a queued timer has no entry");
  const Timer fired = found->second;

  if (fired.every > 0)
  {
    found->second.due = fired.due + fired.every;
    m_queue.emplace(found->second.due, id);
  }
  else
  {
    m_timers.erase(found);
  }

  return fired;
}

auto Scheduler::Clear() -> void
{
  m_timers.clear();
  m_queue.clear();
  // Ids are not reused: a handle the script still holds must not start naming
  // somebody else's timer after a rebuild.
}

}  // namespace sg
