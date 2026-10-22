#include "engine/core/game_state.hpp"

#include <gtest/gtest.h>

#include <boost/json.hpp>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "engine/core/command_router.hpp"
#include "engine/core/event.hpp"
#include "engine/core/random.hpp"

namespace
{

[[nodiscard]] auto TwoTeamsTwoPoints() -> sg::Setup
{
  sg::Setup setup;
  setup.title = "test";
  setup.seed = 2027;
  setup.teams = {{"Wolves", {232, 67, 46}}, {"Foxes", {242, 140, 40}}};
  setup.points = {{"P1", "Old oak", 0}, {"P2", "Stream", 1}};
  setup.params = boost::json::object{{"capture_minutes", 5}};
  return setup;
}

// --- Random ---

TEST(Random, TheSameSeedGivesTheSameSequence)
{
  sg::Random first{2027};
  sg::Random second{2027};
  sg::Random other{2028};

  std::vector<std::uint64_t> a;
  std::vector<std::uint64_t> b;
  for (std::int32_t i = 0; i < 8; ++i)
  {
    a.push_back(first.NextU64());
    b.push_back(second.NextU64());
  }
  EXPECT_EQ(a, b);
  EXPECT_NE(a[0], other.NextU64());
}

TEST(Random, StaysInsideItsRange)
{
  sg::Random random{1};
  std::set<std::int64_t> seen;
  for (std::int32_t i = 0; i < 2000; ++i)
  {
    const std::int64_t roll = random.NextInRange(-2, 3);
    ASSERT_GE(roll, -2);
    ASSERT_LE(roll, 3);
    seen.insert(roll);

    const double uniform = random.NextUniform(0.9, 1.1);
    ASSERT_GE(uniform, 0.9);
    ASSERT_LT(uniform, 1.1);
  }
  // Every value of a six-wide range should turn up in 2000 draws.
  EXPECT_EQ(seen.size(), 6U);
}

TEST(Random, SingleValueRangeAndRejectedBounds)
{
  sg::Random random{7};
  EXPECT_EQ(random.NextInRange(4, 4), 4);
  EXPECT_LT(random.NextDouble(), 1.0);
  EXPECT_DEATH(random.NextBelow(0), "bound above zero");
  EXPECT_DEATH(random.NextInRange(3, 2), "empty range");
}

// --- GameState ---

TEST(GameState, InitialBuildsTeamsAndPointsFromTheSetup)
{
  const sg::GameState state = sg::GameState::Initial(TwoTeamsTwoPoints());

  ASSERT_EQ(state.Teams().size(), 2U);
  EXPECT_EQ(state.Teams()[0].name, "Wolves");
  EXPECT_TRUE(state.Teams()[0].resources.empty());

  ASSERT_EQ(state.Points().size(), 2U);
  EXPECT_EQ(state.Points()[1].name, "Stream");
  EXPECT_FALSE(state.Points()[1].owner.has_value());
  EXPECT_EQ(state.Points()[1].led.pattern, sg::Pattern::kOff);

  EXPECT_EQ(state.Params().at("capture_minutes").as_int64(), 5);
}

TEST(GameState, FindsTeamsAndPointsOrReportsThemAbsent)
{
  sg::GameState state = sg::GameState::Initial(TwoTeamsTwoPoints());

  sg::OptionalRef<sg::TeamState> wolves = state.FindTeam("Wolves");
  ASSERT_TRUE(wolves.has_value());
  wolves->resources["wood"] = 10;
  EXPECT_EQ(state.Teams()[0].resources.at("wood"), 10);

  EXPECT_FALSE(state.FindTeam("Bears").has_value());
  EXPECT_TRUE(state.FindPoint("P2").has_value());
  EXPECT_FALSE(state.FindPoint("P9").has_value());
}

TEST(GameState, SnapshotDoesNotDependOnTheOrderKeysWereWritten)
{
  sg::GameState first = sg::GameState::Initial(TwoTeamsTwoPoints());
  first.Data()["turn"] = 3;
  first.Data()["phase"] = "night";
  first.Teams()[0].resources["wood"] = 10;
  first.Teams()[0].resources["food"] = 5;

  sg::GameState second = sg::GameState::Initial(TwoTeamsTwoPoints());
  second.Data()["phase"] = "night";
  second.Data()["turn"] = 3;
  second.Teams()[0].resources["food"] = 5;
  second.Teams()[0].resources["wood"] = 10;

  EXPECT_EQ(first.Snapshot(), second.Snapshot());
  EXPECT_EQ(first.Hash(), second.Hash());
}

TEST(GameState, HashNoticesEveryPartOfTheState)
{
  const sg::GameState base = sg::GameState::Initial(TwoTeamsTwoPoints());

  const auto changed = [](auto&& mutate)
  {
    sg::GameState state = sg::GameState::Initial(TwoTeamsTwoPoints());
    mutate(state);
    return state.Hash();
  };

  EXPECT_NE(base.Hash(), changed(
                             [](sg::GameState& s)
                             {
                               s.Points()[0].owner = "Wolves";
                             }));
  EXPECT_NE(base.Hash(), changed(
                             [](sg::GameState& s)
                             {
                               s.Points()[0].data["claimant"] = "Foxes";
                             }));
  EXPECT_NE(base.Hash(), changed(
                             [](sg::GameState& s)
                             {
                               s.Points()[0].led.pattern = sg::Pattern::kPulse;
                             }));
  EXPECT_NE(base.Hash(), changed(
                             [](sg::GameState& s)
                             {
                               s.Teams()[1].resources["gold"] = 1;
                             }));
  EXPECT_NE(base.Hash(), changed(
                             [](sg::GameState& s)
                             {
                               s.Data()["turn"] = 2;
                             }));
  EXPECT_NE(base.Hash(), changed(
                             [](sg::GameState& s)
                             {
                               s.Params()["capture_minutes"] = 6;
                             }));
  // Drawing a value moves the generator, and that difference must show: two
  // states that disagree on it would disagree on the next roll.
  EXPECT_NE(base.Hash(), changed(
                             [](sg::GameState& s)
                             {
                               static_cast<void>(s.Rng().NextU64());
                             }));
}

// --- CommandRouter ---

class RecordingSink final : public sg::CommandSink
{
 public:
  auto Send(const sg::Command& command) -> void override
  {
    m_sent.push_back(command);
  }

  [[nodiscard]] auto Sent() const -> const std::vector<sg::Command>&
  {
    return m_sent;
  }

 private:
  std::vector<sg::Command> m_sent;
};

TEST(CommandRouter, ForwardsWhenUnmutedAndDropsWhenMuted)
{
  RecordingSink sink;
  sg::CommandRouter router{sink};

  router.Send(sg::Command{sg::Sound{"turn.wav"}});
  EXPECT_EQ(sink.Sent().size(), 1U);

  router.Mute();
  EXPECT_TRUE(router.Muted());
  router.Send(sg::Command{sg::Sound{"explosion.wav"}});
  EXPECT_EQ(sink.Sent().size(), 1U) << "a replay must be silent";

  router.Unmute();
  router.Send(sg::Command{sg::Sound{"turn.wav"}});
  EXPECT_EQ(sink.Sent().size(), 2U);
}

TEST(CommandRouter, PushFullStateRestatesEveryPointInOrder)
{
  RecordingSink sink;
  sg::CommandRouter router{sink};

  sg::GameState state = sg::GameState::Initial(TwoTeamsTwoPoints());
  state.Points()[0].led = sg::LedState{"P1", {1, 2, 3}, sg::Pattern::kSolid, 4};
  state.Points()[1].led = sg::LedState{"P2", {5, 6, 7}, sg::Pattern::kPulse, 8};

  router.PushFullState(state);

  ASSERT_EQ(sink.Sent().size(), 2U);
  EXPECT_EQ(std::get<sg::LedState>(sink.Sent()[0]).point, "P1");
  EXPECT_EQ(std::get<sg::LedState>(sink.Sent()[1]).version, 8);
}

}  // namespace
