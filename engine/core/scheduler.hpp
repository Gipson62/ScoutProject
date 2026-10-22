#ifndef SG_ENGINE_CORE_SCHEDULER_HPP_
#define SG_ENGINE_CORE_SCHEDULER_HPP_

// The timer queue. It decides nothing about what a timer does — that is the
// script's business — only when each one fires and in which order, and it does
// so identically on every run: ties go to the lower timer id, never to luck.

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <utility>

#include "engine/core/event.hpp"

namespace sg
{

using TimerId = std::uint64_t;

struct Timer
{
  // When it is due, in game time.
  Millis due = 0;
  // Repeat interval; 0 for a one-shot.
  Millis every = 0;
  TimerId id = 0;
};

class Scheduler
{
 public:
  // Schedules a one-shot at `due`, and a repeat whose first firing is at `due`
  // and which then repeats every `interval`. `interval` must be positive: a
  // zero or negative one would be due for ever at the same instant.
  [[nodiscard]] auto After(Millis due) -> TimerId;
  [[nodiscard]] auto Every(Millis due, Millis interval) -> TimerId;

  // Returns false when there is no such timer: cancelling a timer that already
  // fired is normal, not an error.
  auto Cancel(TimerId id) -> bool;

  [[nodiscard]] auto Empty() const -> bool
  {
    return m_queue.empty();
  }
  [[nodiscard]] auto PendingCount() const -> std::size_t
  {
    return m_queue.size();
  }
  [[nodiscard]] auto NextDue() const -> std::optional<Millis>;
  [[nodiscard]] auto IsPending(TimerId id) const -> bool
  {
    return m_timers.contains(id);
  }

  // Takes the next timer due at or before `t`, or nothing. A repeating timer is
  // put back for `due + every` **before** this returns, so a callback that
  // cancels its own timer while running really stops it; the Timer returned
  // still carries the due time it fired at, which is the time the script sees.
  //
  // A timer whose next firing is also at or before `t` comes back from the next
  // call: five minutes of catch-up after a late event fires a one-minute income
  // timer five times, which is what makes a replay pay the same income as the
  // live run did.
  [[nodiscard]] auto PopDue(Millis t) -> std::optional<Timer>;

  // Forgets every timer and restarts the ids, so a rebuild hands out the same
  // ids the live run did: a mode keeps its handles in point.data, which is part
  // of the state a replay must reproduce exactly.
  auto Clear() -> void;

 private:
  // Ordered by (due, id), which is the firing order. std::map and std::set, not
  // their unordered cousins: replay order must not depend on a hash.
  std::map<TimerId, Timer> m_timers;
  std::set<std::pair<Millis, TimerId>> m_queue;
  TimerId m_next_id = 1;
};

}  // namespace sg

#endif  // SG_ENGINE_CORE_SCHEDULER_HPP_
