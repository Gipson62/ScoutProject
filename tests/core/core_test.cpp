#include "engine/core/core.hpp"

#include <gtest/gtest.h>

#include <boost/json.hpp>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "engine/core/clock.hpp"
#include "engine/core/command_router.hpp"
#include "engine/core/error.hpp"
#include "engine/core/event.hpp"
#include "engine/core/game_state.hpp"
#include "engine/core/mode.hpp"
#include "engine/core/optional_ref.hpp"
#include "engine/core/random.hpp"
#include "engine/core/scheduler.hpp"
#include "engine/log/event_log.hpp"

namespace
{

constexpr sg::Millis kCaptureDelay = 300'000;  // five minutes
constexpr sg::Millis kIncomeEvery = 60'000;    // one minute

// A capture mode written in C++, so the Core can be tested before pocketpy
// exists: tap to claim, five uncontested minutes to own, a rival tap restarts
// the countdown, and income every minute while held. Every piece of state it
// keeps lives in GameState, which is what makes it replayable.
class CaptureStub final : public sg::Mode
{
 public:
  auto OnStart(sg::GameState& state, sg::ModeHost& host) -> void override
  {
    for (sg::PointState& point : state.Points())
    {
      point.data["claimant"] = nullptr;
      point.data["timer"] = 0;
      host.Emit(sg::Command{
          sg::LedState{point.id, {255, 255, 255}, sg::Pattern::kSolid, 0}});
    }
    state.Data()["income_timer"] =
        static_cast<std::int64_t>(host.Every(kIncomeEvery));
    state.Data()["confirmed"] = 0;
  }

  auto OnTag(sg::GameState& state, sg::ModeHost& host,
             std::string_view point_id, const sg::Tag& tag) -> void override
  {
    sg::OptionalRef<sg::PointState> point = state.FindPoint(point_id);
    if (!point.has_value() || !tag.Known())
    {
      host.Emit(
          sg::Command{sg::Beep{std::string{point_id}, sg::BeepKind::kDeny}});
      return;
    }

    const bool holds = point->owner.has_value() && *point->owner == tag.team;
    const bool claims = point->data.at("claimant").is_string() &&
                        point->data.at("claimant").as_string() == tag.team;
    if (holds || claims)
    {
      host.Emit(sg::Command{sg::Beep{point->id, sg::BeepKind::kDeny}});
      return;
    }

    const auto running =
        static_cast<sg::TimerId>(point->data.at("timer").as_int64());
    if (running != 0)
    {
      static_cast<void>(host.Cancel(running));
    }

    point->data["claimant"] = tag.team;
    point->data["timer"] = static_cast<std::int64_t>(host.After(kCaptureDelay));
    const sg::Rgb colour = ColourOf(state, tag.team);
    host.Emit(
        sg::Command{sg::LedState{point->id, colour, sg::Pattern::kPulse, 0}});
    host.Emit(sg::Command{sg::Beep{point->id, sg::BeepKind::kOk}});
  }

  auto OnButton(sg::GameState& state, sg::ModeHost& host,
                std::string_view point_id) -> void override
  {
    // Identity-free input: a button press only beeps, but it must beep the same
    // way on a replay, so it reads the state like any other hook.
    static_cast<void>(state);
    host.Emit(
        sg::Command{sg::Beep{std::string{point_id}, sg::BeepKind::kAlarm}});
  }

  auto OnTimer(sg::GameState& state, sg::ModeHost& host, sg::TimerId id)
      -> void override
  {
    if (static_cast<sg::TimerId>(state.Data().at("income_timer").as_int64()) ==
        id)
    {
      for (const sg::PointState& point : state.Points())
      {
        if (!point.owner.has_value())
        {
          continue;
        }
        if (sg::OptionalRef<sg::TeamState> team = state.FindTeam(*point.owner))
        {
          team->resources["wood"] += 10;
        }
      }
      return;
    }

    for (sg::PointState& point : state.Points())
    {
      if (static_cast<sg::TimerId>(point.data.at("timer").as_int64()) != id)
      {
        continue;
      }
      const boost::json::value& claimant = point.data.at("claimant");
      if (!claimant.is_string())
      {
        return;
      }
      point.owner = std::string{claimant.as_string().c_str()};
      point.data["claimant"] = nullptr;
      point.data["timer"] = 0;
      state.Data()["confirmed"] = state.Data().at("confirmed").as_int64() + 1;
      host.Emit(sg::Command{sg::LedState{
          point.id, ColourOf(state, *point.owner), sg::Pattern::kSolid, 0}});
      host.Emit(sg::Command{
          sg::Announce{*point.owner + " holds " + point.name, "all"}});
      return;
    }
  }

 private:
  [[nodiscard]] static auto ColourOf(const sg::GameState& state,
                                     const std::string& team) -> sg::Rgb
  {
    if (sg::OptionalRef<const sg::TeamState> found = state.FindTeam(team))
    {
      return found->colour;
    }
    return sg::Rgb{};
  }
};

class StubRoster final : public sg::Roster
{
 public:
  explicit StubRoster(std::map<std::string, std::string> uid_to_team)
      : m_uid_to_team(std::move(uid_to_team))
  {
  }

  [[nodiscard]] auto Lookup(std::string_view uid) const -> sg::Tag override
  {
    const auto found = m_uid_to_team.find(std::string{uid});
    if (found == m_uid_to_team.end())
    {
      return sg::Tag{std::string{uid}, "", "", ""};
    }
    return sg::Tag{std::string{uid}, found->second, "someone", "player"};
  }

 private:
  std::map<std::string, std::string> m_uid_to_team;
};

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
  auto Clear() -> void
  {
    m_sent.clear();
  }

 private:
  std::vector<sg::Command> m_sent;
};

[[nodiscard]] auto MakeSetup() -> sg::Setup
{
  sg::Setup setup;
  setup.title = "core test";
  setup.seed = 2027;
  setup.teams = {{"Wolves", {232, 67, 46}}, {"Foxes", {242, 140, 40}}};
  setup.points = {
      {"P1", "Old oak", 0}, {"P2", "Stream", 1}, {"P3", "Hilltop", 2}};
  return setup;
}

[[nodiscard]] auto MakeRoster() -> StubRoster
{
  return StubRoster{{{"W1", "Wolves"}, {"W2", "Wolves"}, {"F1", "Foxes"}}};
}

// One engine, assembled the way main.cpp will assemble it.
class Rig
{
 public:
  explicit Rig(const std::filesystem::path& path, sg::Millis now = 0)
      : m_clock(now),
        m_log(sg::EventLog::Open(path).value()),
        m_router(m_sink),
        m_core(m_clock, m_log, m_router, m_roster, MakeSetup(),
               []
               {
                 return std::make_unique<CaptureStub>();
               })
  {
  }

  auto Feed(sg::Millis t, std::string_view point, std::string_view uid) -> void
  {
    m_clock.SetNow(t);
    ASSERT_TRUE(m_core
                    .OnInput(sg::Event{
                        0, t, sg::Tap{std::string{point}, std::string{uid}}})
                    .has_value());
  }

  auto FeedButton(sg::Millis t, std::string_view point) -> void
  {
    m_clock.SetNow(t);
    ASSERT_TRUE(m_core.OnInput(sg::Event{0, t, sg::Button{std::string{point}}})
                    .has_value());
  }

  sg::FakeClock m_clock;
  RecordingSink m_sink;
  sg::EventLog m_log;
  StubRoster m_roster = MakeRoster();
  sg::CommandRouter m_router;
  sg::Core m_core;
};

class CoreTest : public ::testing::Test
{
 protected:
  auto SetUp() -> void override
  {
    m_path =
        std::filesystem::temp_directory_path() /
        ("sgengine-core-" +
         std::string{
             ::testing::UnitTest::GetInstance()->current_test_info()->name()} +
         ".jsonl");
    std::filesystem::remove(m_path);
  }

  auto TearDown() -> void override
  {
    std::filesystem::remove(m_path);
  }

  std::filesystem::path m_path;
};

TEST_F(CoreTest, StartRunsTheModeAndLightsEveryPoint)
{
  Rig rig{m_path};
  rig.m_core.Start();

  // OnStart's LEDs were emitted while the router was muted, so what reaches the
  // sink is the single resync PushFullState sends afterwards.
  ASSERT_EQ(rig.m_sink.Sent().size(), 3U);
  EXPECT_EQ(std::get<sg::LedState>(rig.m_sink.Sent()[0]).pattern,
            sg::Pattern::kSolid);
  EXPECT_EQ(rig.m_core.Rebuilds(), 1U);
}

TEST_F(CoreTest, ATapClaimsAPointAndFiveMinutesLaterConfirmsIt)
{
  Rig rig{m_path};
  rig.m_core.Start();
  rig.m_sink.Clear();

  rig.Feed(1000, "P1", "W1");
  EXPECT_EQ(rig.m_core.State().Points()[0].data.at("claimant").as_string(),
            "Wolves");
  EXPECT_FALSE(rig.m_core.State().Points()[0].owner.has_value());

  // Nothing confirms a second early.
  rig.m_clock.SetNow(1000 + kCaptureDelay - 1);
  rig.m_core.Tick();
  EXPECT_FALSE(rig.m_core.State().Points()[0].owner.has_value());

  rig.m_clock.SetNow(1000 + kCaptureDelay);
  rig.m_core.Tick();
  ASSERT_TRUE(rig.m_core.State().Points()[0].owner.has_value());
  EXPECT_EQ(*rig.m_core.State().Points()[0].owner, "Wolves");
}

TEST_F(CoreTest, ARivalTapRestartsTheCountdown)
{
  Rig rig{m_path};
  rig.m_core.Start();

  rig.Feed(1000, "P1", "W1");
  rig.Feed(200'000, "P1", "F1");  // a Fox taps before the Wolves' five minutes

  rig.m_clock.SetNow(1000 + kCaptureDelay);
  rig.m_core.Tick();
  EXPECT_FALSE(rig.m_core.State().Points()[0].owner.has_value())
      << "the cancelled countdown must not confirm";

  rig.m_clock.SetNow(200'000 + kCaptureDelay);
  rig.m_core.Tick();
  ASSERT_TRUE(rig.m_core.State().Points()[0].owner.has_value());
  EXPECT_EQ(*rig.m_core.State().Points()[0].owner, "Foxes");
}

TEST_F(CoreTest, AnUnknownWristbandOnlyBeeps)
{
  Rig rig{m_path};
  rig.m_core.Start();
  rig.m_sink.Clear();

  rig.Feed(1000, "P1", "NOPE");

  ASSERT_EQ(rig.m_sink.Sent().size(), 1U);
  EXPECT_EQ(std::get<sg::Beep>(rig.m_sink.Sent()[0]).kind, sg::BeepKind::kDeny);
  EXPECT_FALSE(rig.m_core.State().Points()[0].data.at("claimant").is_string());
}

TEST_F(CoreTest, TheEngineAssignsLedVersionsAndRemembersTheLastOne)
{
  Rig rig{m_path};
  rig.m_core.Start();
  rig.m_sink.Clear();

  rig.Feed(1000, "P1", "W1");
  const sg::LedState& led = std::get<sg::LedState>(rig.m_sink.Sent()[0]);
  EXPECT_EQ(led.point, "P1");
  EXPECT_EQ(led.pattern, sg::Pattern::kPulse);
  EXPECT_EQ(led.version, 2) << "OnStart sent version 1 for this point";
  EXPECT_EQ(rig.m_core.State().Points()[0].led.version, 2);
}

TEST_F(CoreTest, LiveRunAndRebuildReachTheSameState)
{
  Rig rig{m_path};
  rig.m_core.Start();

  rig.Feed(1'000, "P1", "W1");
  rig.Feed(50'000, "P2", "F1");
  rig.Feed(120'000, "P1", "F1");
  rig.FeedButton(130'000, "P3");
  rig.m_clock.SetNow(600'000);
  rig.m_core.Tick();
  rig.Feed(700'000, "P3", "W2");
  rig.m_clock.SetNow(1'200'000);
  rig.m_core.Tick();

  const std::uint64_t live = rig.m_core.State().Hash();
  const std::string live_snapshot = rig.m_core.State().Snapshot();
  EXPECT_EQ(rig.m_core.Rebuilds(), 1U)
      << "nothing here should have forced a rebuild";

  rig.m_sink.Clear();
  rig.m_core.Rebuild();

  EXPECT_EQ(rig.m_core.State().Snapshot(), live_snapshot);
  EXPECT_EQ(rig.m_core.State().Hash(), live);
  // The replay itself was silent; only the resync came out.
  EXPECT_EQ(rig.m_sink.Sent().size(), rig.m_core.State().Points().size());
}

TEST_F(CoreTest, AFreshEngineOverTheSameLogReachesTheSameState)
{
  std::uint64_t live = 0;
  {
    Rig rig{m_path};
    rig.m_core.Start();
    rig.Feed(1'000, "P1", "W1");
    rig.Feed(60'000, "P2", "F1");
    rig.m_clock.SetNow(400'000);
    rig.m_core.Tick();
    live = rig.m_core.State().Hash();
  }

  // What happens after a power cut: a new process, the same session file.
  Rig restarted{m_path, 400'000};
  restarted.m_core.Start();

  EXPECT_EQ(restarted.m_core.State().Hash(), live);
}

TEST_F(CoreTest, ALateEventRebuildsAndTakesItsTruePlaceInThePast)
{
  Rig rig{m_path};
  rig.m_core.Start();

  rig.Feed(1'000, "P1", "W1");
  rig.m_clock.SetNow(200'000);
  rig.m_core.Tick();
  const std::uint64_t rebuilds_before = rig.m_core.Rebuilds();

  // A Fox tap that happened at 100 s but whose packet only arrives now. It has
  // to cancel the Wolves' countdown as if it had arrived on time.
  rig.m_clock.SetNow(200'000);
  ASSERT_TRUE(rig.m_core.OnInput(sg::Event{0, 100'000, sg::Tap{"P1", "F1"}})
                  .has_value());
  EXPECT_EQ(rig.m_core.Rebuilds(), rebuilds_before + 1);
  EXPECT_EQ(rig.m_core.State().Points()[0].data.at("claimant").as_string(),
            "Foxes");

  rig.m_clock.SetNow(1'000 + kCaptureDelay);
  rig.m_core.Tick();
  EXPECT_FALSE(rig.m_core.State().Points()[0].owner.has_value());

  rig.m_clock.SetNow(100'000 + kCaptureDelay);
  rig.m_core.Tick();
  ASSERT_TRUE(rig.m_core.State().Points()[0].owner.has_value());
  EXPECT_EQ(*rig.m_core.State().Points()[0].owner, "Foxes");
}

TEST_F(CoreTest, AVoidRebuildsWithoutTheEventItTargets)
{
  Rig rig{m_path};
  rig.m_core.Start();

  rig.Feed(1'000, "P1", "W1");
  const std::uint64_t mistake = rig.m_log.Events().back().seq;
  rig.m_clock.SetNow(400'000);
  rig.m_core.Tick();
  ASSERT_TRUE(rig.m_core.State().Points()[0].owner.has_value());

  rig.m_clock.SetNow(500'000);
  ASSERT_TRUE(
      rig.m_core.OnInput(sg::Event{0, 500'000, sg::Void{mistake}}).has_value());

  EXPECT_FALSE(rig.m_core.State().Points()[0].owner.has_value())
      << "voiding the capture must undo everything downstream of it";
  // The line is still in the log: voiding appends, it never edits.
  EXPECT_EQ(rig.m_log.Events().size(), 2U);
}

TEST_F(CoreTest, AThousandRandomEventsReplayTwiceToTheSameState)
{
  // The determinism test the design's build order asks for.
  Rig rig{m_path};
  rig.m_core.Start();

  sg::Random random{12345};
  const std::array<std::string, 3> points{"P1", "P2", "P3"};
  const std::array<std::string, 4> uids{"W1", "W2", "F1", "NOPE"};
  sg::Millis t = 0;
  for (std::int32_t i = 0; i < 1000; ++i)
  {
    t += random.NextInRange(1, 4000);
    const std::string& point =
        points[static_cast<std::size_t>(random.NextBelow(points.size()))];
    if (random.NextBelow(10) == 0)
    {
      rig.FeedButton(t, point);
    }
    else
    {
      rig.Feed(t, point,
               uids[static_cast<std::size_t>(random.NextBelow(uids.size()))]);
    }
  }
  rig.m_clock.SetNow(t + 1'000'000);
  rig.m_core.Tick();

  const std::uint64_t live = rig.m_core.State().Hash();
  rig.m_core.Rebuild();
  const std::uint64_t first = rig.m_core.State().Hash();
  rig.m_core.Rebuild();
  const std::uint64_t second = rig.m_core.State().Hash();

  EXPECT_EQ(live, first);
  EXPECT_EQ(first, second);
  EXPECT_GT(rig.m_core.State().Data().at("confirmed").as_int64(), 0)
      << "the run should actually have captured something";
}

}  // namespace
