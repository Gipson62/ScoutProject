#include "engine/core/scheduler.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <vector>

#include "engine/core/clock.hpp"
#include "engine/core/event.hpp"

namespace
{

// Drains everything due at or before `t`, the way Core::RunUntil will.
[[nodiscard]] auto DrainUntil(sg::Scheduler& scheduler, sg::Millis t)
    -> std::vector<sg::Timer>
{
  std::vector<sg::Timer> fired;
  while (const std::optional<sg::Timer> timer = scheduler.PopDue(t))
  {
    fired.push_back(*timer);
  }
  return fired;
}

TEST(Scheduler, FiresNothingBeforeItsDueTime)
{
  sg::Scheduler scheduler;
  const sg::TimerId id = scheduler.After(1000);

  EXPECT_EQ(scheduler.NextDue(), 1000);
  EXPECT_TRUE(scheduler.IsPending(id));
  EXPECT_TRUE(DrainUntil(scheduler, 999).empty());

  const std::vector<sg::Timer> fired = DrainUntil(scheduler, 1000);
  ASSERT_EQ(fired.size(), 1U);
  EXPECT_EQ(fired[0].id, id);
  EXPECT_EQ(fired[0].due, 1000);
  EXPECT_FALSE(scheduler.IsPending(id));
  EXPECT_TRUE(scheduler.Empty());
}

TEST(Scheduler, OrdersByDueTimeThenById)
{
  sg::Scheduler scheduler;
  const sg::TimerId late = scheduler.After(300);
  const sg::TimerId early = scheduler.After(100);
  const sg::TimerId tie = scheduler.After(300);

  const std::vector<sg::Timer> fired = DrainUntil(scheduler, 1000);
  ASSERT_EQ(fired.size(), 3U);
  EXPECT_EQ(fired[0].id, early);
  EXPECT_EQ(fired[1].id, late);  // same due time, lower id first
  EXPECT_EQ(fired[2].id, tie);
}

TEST(Scheduler, GivesTheSameOrderWhateverTheInsertionOrder)
{
  // Determinism is the point of the whole log: the same timers must fire in the
  // same order whether they were created live or during a replay.
  const auto run = [](bool reversed)
  {
    sg::Scheduler scheduler;
    std::vector<sg::Millis> dues{500, 100, 300, 100, 700};
    if (reversed)
    {
      std::ranges::reverse(dues);
    }
    for (const sg::Millis due : dues)
    {
      static_cast<void>(scheduler.After(due));
    }
    std::vector<sg::Millis> order;
    for (const sg::Timer& timer : DrainUntil(scheduler, 1000))
    {
      order.push_back(timer.due);
    }
    return order;
  };

  const std::vector<sg::Millis> expected{100, 100, 300, 500, 700};
  EXPECT_EQ(run(false), expected);
  EXPECT_EQ(run(true), expected);
}

TEST(Scheduler, RepeatsAtItsInterval)
{
  sg::Scheduler scheduler;
  const sg::TimerId id = scheduler.Every(1000, 1000);

  const std::vector<sg::Timer> fired = DrainUntil(scheduler, 3000);
  ASSERT_EQ(fired.size(), 3U);
  EXPECT_EQ(fired[0].due, 1000);
  EXPECT_EQ(fired[1].due, 2000);
  EXPECT_EQ(fired[2].due, 3000);
  for (const sg::Timer& timer : fired)
  {
    EXPECT_EQ(timer.id, id);
    EXPECT_EQ(timer.every, 1000);
  }
  // Still pending: a repeat never runs out.
  EXPECT_TRUE(scheduler.IsPending(id));
  EXPECT_EQ(scheduler.NextDue(), 4000);
}

TEST(Scheduler, CatchesUpAfterAGap)
{
  // A tap buffered for four minutes arrives, and the one-minute income timer
  // has to pay the four minutes it owes, or a replay would pay less than the
  // live run did.
  sg::Scheduler scheduler;
  static_cast<void>(scheduler.Every(60'000, 60'000));

  EXPECT_EQ(DrainUntil(scheduler, 240'000).size(), 4U);
}

TEST(Scheduler, CancelRemovesAPendingTimer)
{
  sg::Scheduler scheduler;
  const sg::TimerId kept = scheduler.After(100);
  const sg::TimerId dropped = scheduler.After(200);

  EXPECT_TRUE(scheduler.Cancel(dropped));
  EXPECT_FALSE(scheduler.IsPending(dropped));
  EXPECT_EQ(scheduler.PendingCount(), 1U);

  const std::vector<sg::Timer> fired = DrainUntil(scheduler, 1000);
  ASSERT_EQ(fired.size(), 1U);
  EXPECT_EQ(fired[0].id, kept);
}

TEST(Scheduler, CancellingATimerThatAlreadyFiredIsNotAnError)
{
  sg::Scheduler scheduler;
  const sg::TimerId id = scheduler.After(100);
  static_cast<void>(DrainUntil(scheduler, 100));

  // A capture script cancels the point's countdown on every rival tap without
  // knowing whether it has already confirmed.
  EXPECT_FALSE(scheduler.Cancel(id));
  EXPECT_FALSE(scheduler.Cancel(9999));
}

TEST(Scheduler, ARepeatCancelledWhileFiringDoesNotComeBack)
{
  // PopDue requeues a repeat before handing it over, so a callback that
  // cancels its own timer really stops it.
  sg::Scheduler scheduler;
  const sg::TimerId id = scheduler.Every(1000, 1000);

  const std::optional<sg::Timer> fired = scheduler.PopDue(1000);
  ASSERT_TRUE(fired.has_value());
  EXPECT_TRUE(scheduler.Cancel(fired->id));
  EXPECT_FALSE(scheduler.IsPending(id));
  EXPECT_TRUE(DrainUntil(scheduler, 10'000).empty());
}

TEST(Scheduler, ClearForgetsEverythingAndKeepsIdsUnique)
{
  sg::Scheduler scheduler;
  const sg::TimerId before = scheduler.After(100);
  scheduler.Clear();

  EXPECT_TRUE(scheduler.Empty());
  EXPECT_FALSE(scheduler.IsPending(before));

  // A handle the script still holds must not start naming a new timer.
  const sg::TimerId after = scheduler.After(100);
  EXPECT_NE(after, before);
}

TEST(FakeClock, StartsWhereItIsToldAndMovesForwardOnly)
{
  sg::FakeClock clock{500};
  EXPECT_EQ(clock.Now(), 500);

  clock.Advance(250);
  EXPECT_EQ(clock.Now(), 750);

  clock.SetNow(2000);
  EXPECT_EQ(clock.Now(), 2000);

  EXPECT_DEATH(clock.Advance(-1), "negative step");
  EXPECT_DEATH(clock.SetNow(1999), "backwards");
}

TEST(SteadyClock, StartsNearZeroAndNeverGoesBackwards)
{
  const sg::SteadyClock clock;
  const sg::Millis first = clock.Now();
  EXPECT_GE(first, 0);
  EXPECT_LT(first, 1000);
  EXPECT_GE(clock.Now(), first);
}

}  // namespace
