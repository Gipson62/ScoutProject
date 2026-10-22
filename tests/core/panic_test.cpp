#include "engine/core/panic.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace
{

TEST(Panic, PrintsTheMessageAndTheCallSite)
{
  EXPECT_DEATH(sg::Panic("the timer queue is empty"),
               "sgengine panic: the timer queue is empty.*panic_test.cpp");
}

TEST(Panic, AssertPassesWhenTheInvariantHolds)
{
  std::int32_t calls = 0;
  SG_ASSERT(++calls == 1, "SG_ASSERT must evaluate its condition exactly once");
  EXPECT_EQ(calls, 1);
}

TEST(Panic, AssertPanicsWithItsOwnMessage)
{
  EXPECT_DEATH(SG_ASSERT(1 == 2, "two timers share an id"),
               "two timers share an id");
}

}  // namespace
