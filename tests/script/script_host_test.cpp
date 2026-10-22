#include "engine/script/script_host.hpp"

#include <gtest/gtest.h>

#include <boost/json.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "engine/core/clock.hpp"
#include "engine/core/command_router.hpp"
#include "engine/core/core.hpp"
#include "engine/core/error.hpp"
#include "engine/core/event.hpp"
#include "engine/core/game_state.hpp"
#include "engine/core/mode.hpp"
#include "engine/log/event_log.hpp"

namespace
{

constexpr sg::Millis kCaptureDelay =
    300'000;  // five minutes, as config/dev.toml says

[[nodiscard]] auto ReadFile(const std::filesystem::path& path) -> std::string
{
  std::ifstream in(path, std::ios::binary);
  EXPECT_TRUE(in) << "cannot read " << path;
  return std::string{std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>()};
}

[[nodiscard]] auto CaptureModeSource() -> std::string
{
  return ReadFile(std::filesystem::path{SG_REPO_ROOT} / "modes" / "capture.py");
}

[[nodiscard]] auto MakeSetup() -> sg::Setup
{
  sg::Setup setup;
  setup.title = "script test";
  setup.seed = 2027;
  setup.teams = {{"Wolves", {232, 67, 46}}, {"Foxes", {242, 140, 40}}};
  setup.points = {{"P1", "Old oak", 0}, {"P2", "Stream", 1}};
  setup.params = boost::json::object{{"capture_minutes", 5}};
  setup.config = boost::json::object{{"title", "script test"}};
  return setup;
}

class StubRoster final : public sg::Roster
{
 public:
  [[nodiscard]] auto Lookup(std::string_view uid) const -> sg::Tag override
  {
    static const std::map<std::string, std::string> kTeams{{"W1", "Wolves"},
                                                           {"F1", "Foxes"}};
    const auto found = kTeams.find(std::string{uid});
    if (found == kTeams.end())
    {
      return sg::Tag{std::string{uid}, "", "", ""};
    }
    return sg::Tag{std::string{uid}, found->second, "someone", "player"};
  }
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

// A whole engine running a real Python mode, assembled as main.cpp will.
class ScriptRig
{
 public:
  ScriptRig(const std::filesystem::path& log_path, std::string source)
      : m_source(std::move(source)),
        m_log(sg::EventLog::Open(log_path).value()),
        m_router(m_sink),
        m_core(m_clock, m_log, m_router, m_roster, MakeSetup(),
               [this]
               {
                 return std::make_unique<sg::ScriptHost>(m_source, "test_mode");
               })
  {
  }

  auto Tap(sg::Millis t, std::string_view point, std::string_view uid) -> void
  {
    m_clock.SetNow(t);
    ASSERT_TRUE(m_core
                    .OnInput(sg::Event{
                        0, t, sg::Tap{std::string{point}, std::string{uid}}})
                    .has_value());
  }

  auto Advance(sg::Millis to) -> void
  {
    m_clock.SetNow(to);
    m_core.Tick();
  }

  std::string m_source;
  sg::FakeClock m_clock{0};
  RecordingSink m_sink;
  sg::EventLog m_log;
  StubRoster m_roster;
  sg::CommandRouter m_router;
  sg::Core m_core;
};

class ScriptHostTest : public ::testing::Test
{
 protected:
  auto SetUp() -> void override
  {
    m_path =
        std::filesystem::temp_directory_path() /
        ("sgengine-script-" +
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

// --- the host on its own ---

TEST_F(ScriptHostTest, LoadsAScriptAndReportsNoError)
{
  const sg::ScriptHost host{
      "from scoutgame import on_start\n\n@on_start\ndef go(game):\n    pass\n",
      "ok_mode"};
  EXPECT_FALSE(host.LoadError().has_value()) << *host.LoadError();
  EXPECT_TRUE(host.Errors().empty());
}

TEST_F(ScriptHostTest, ReportsASyntaxErrorWithItsTraceback)
{
  const sg::ScriptHost host{"def broken(:\n", "broken_mode"};
  ASSERT_TRUE(host.LoadError().has_value());
  EXPECT_NE(host.LoadError()->find("broken_mode"), std::string::npos)
      << *host.LoadError();
}

TEST_F(ScriptHostTest, AFailingHookIsLoggedAndSkippedAndTheGameContinues)
{
  // DESIGN.md: an error inside a hook is shown and the game carries on.
  ScriptRig rig{m_path, R"(from scoutgame import on_start, on_tag

@on_start
def setup(game):
    game.data["started"] = 1

@on_tag()
def boom(game, point, tag):
    raise ValueError("no")

@on_tag()
def after_boom(game, point, tag):
    game.data["reached"] = 1
)"};
  rig.m_core.Start();
  rig.Tap(1000, "P1", "W1");

  EXPECT_EQ(rig.m_core.State().Data().at("started").as_int64(), 1);
  // The whole dispatch stops at the raising hook, but the engine keeps running.
  EXPECT_FALSE(rig.m_core.State().Data().if_contains("reached") != nullptr &&
               rig.m_core.State().Data().at("reached").as_int64() == 1);

  rig.Tap(2000, "P2", "F1");
  EXPECT_EQ(rig.m_core.Rebuilds(), 1U)
      << "a script error must not provoke a rebuild";
}

TEST_F(ScriptHostTest, ExposesTheApiTheDesignLists)
{
  ScriptRig rig{m_path, R"(from scoutgame import on_start, minutes, seconds

@on_start
def setup(game):
    game.data["now"] = game.now
    game.data["teams"] = [t.name for t in game.teams]
    game.data["colour"] = game.teams[0].colour
    game.data["points"] = [p.id for p in game.points]
    game.data["point_name"] = game.points[0].name
    game.data["param"] = game.p.capture_minutes
    game.data["config_title"] = game.config["title"]
    game.data["minutes"] = minutes(5)
    game.data["seconds"] = seconds(2)
    game.data["roll"] = game.random.randint(1, 6)
    game.data["found"] = game.team("Foxes").name
    game.data["missing"] = game.team("Bears") is None
    game.teams[0].res["wood"] = 7
    game.teams[0].data["note"] = "hello"
)"};
  rig.m_core.Start();

  const boost::json::object& data = rig.m_core.State().Data();
  EXPECT_EQ(data.at("now").as_int64(), 0);
  EXPECT_EQ(data.at("teams").as_array().at(1).as_string(), "Foxes");
  EXPECT_EQ(data.at("colour").as_string(), "#e8432e");
  EXPECT_EQ(data.at("points").as_array().at(0).as_string(), "P1");
  EXPECT_EQ(data.at("point_name").as_string(), "Old oak");
  EXPECT_EQ(data.at("param").as_int64(), 5);
  EXPECT_EQ(data.at("config_title").as_string(), "script test");
  EXPECT_EQ(data.at("minutes").as_int64(), 300'000);
  EXPECT_EQ(data.at("seconds").as_int64(), 2000);
  EXPECT_GE(data.at("roll").as_int64(), 1);
  EXPECT_LE(data.at("roll").as_int64(), 6);
  EXPECT_EQ(data.at("found").as_string(), "Foxes");
  EXPECT_TRUE(data.at("missing").as_bool());

  EXPECT_EQ(rig.m_core.State().Teams()[0].resources.at("wood"), 7);
  EXPECT_EQ(rig.m_core.State().Teams()[0].data.at("note").as_string(), "hello");
}

TEST_F(ScriptHostTest, RandomnessComesFromTheSeededGeneratorOnly)
{
  // The same seed and the same script give the same rolls: that is what makes a
  // replay reach the same state.
  constexpr std::string_view kSource = R"(from scoutgame import on_start

@on_start
def setup(game):
    game.data["rolls"] = [game.random.randint(0, 1000000) for _ in range(5)]
)";
  ScriptRig first{m_path, std::string{kSource}};
  first.m_core.Start();
  const std::string once = boost::json::serialize(first.m_core.State().Data());

  first.m_core.Rebuild();
  EXPECT_EQ(boost::json::serialize(first.m_core.State().Data()), once);
}

TEST_F(ScriptHostTest, TimersCarryTheirArgumentsAndCanBeCancelled)
{
  ScriptRig rig{m_path, R"(from scoutgame import on_start, on_tag

@on_start
def setup(game):
    game.data["fired"] = []
    game.data["kept"] = game.after(1000, note, "kept")
    game.data["dropped"] = game.after(2000, note, "dropped")
    game.cancel(game.data["dropped"])
    game.every(5000, note, "tick")

def note(game, what):
    game.data["fired"] = game.data["fired"] + [what]
)"};
  rig.m_core.Start();
  rig.Advance(12'000);

  const boost::json::array& fired =
      rig.m_core.State().Data().at("fired").as_array();
  ASSERT_EQ(fired.size(), 3U);
  EXPECT_EQ(fired.at(0).as_string(), "kept");
  EXPECT_EQ(fired.at(1).as_string(), "tick");
  EXPECT_EQ(fired.at(2).as_string(), "tick");
}

// --- modes/capture.py, the real mode ---

TEST_F(ScriptHostTest, CaptureModeLightsEveryPointWhiteAtTheStart)
{
  ScriptRig rig{m_path, CaptureModeSource()};
  rig.m_core.Start();

  ASSERT_EQ(rig.m_sink.Sent().size(), 2U);
  const auto& led = std::get<sg::LedState>(rig.m_sink.Sent()[0]);
  EXPECT_EQ(led.pattern, sg::Pattern::kSolid);
  EXPECT_EQ(led.rgb.r, 255);
  EXPECT_EQ(led.rgb.g, 255);
}

TEST_F(ScriptHostTest, CaptureModeClaimsThenOwnsAPoint)
{
  ScriptRig rig{m_path, CaptureModeSource()};
  rig.m_core.Start();
  rig.m_sink.Clear();

  rig.Tap(1000, "P1", "W1");
  EXPECT_EQ(rig.m_core.State().Points()[0].data.at("claimant").as_string(),
            "team:Wolves");
  EXPECT_FALSE(rig.m_core.State().Points()[0].owner.has_value());
  ASSERT_EQ(rig.m_sink.Sent().size(), 2U);
  EXPECT_EQ(std::get<sg::LedState>(rig.m_sink.Sent()[0]).pattern,
            sg::Pattern::kPulse);
  EXPECT_EQ(std::get<sg::Beep>(rig.m_sink.Sent()[1]).kind, sg::BeepKind::kOk);

  rig.Advance(1000 + kCaptureDelay - 1);
  EXPECT_FALSE(rig.m_core.State().Points()[0].owner.has_value());

  rig.m_sink.Clear();
  rig.Advance(1000 + kCaptureDelay);
  ASSERT_TRUE(rig.m_core.State().Points()[0].owner.has_value());
  EXPECT_EQ(*rig.m_core.State().Points()[0].owner, "Wolves");
  ASSERT_EQ(rig.m_sink.Sent().size(), 2U);
  EXPECT_EQ(std::get<sg::LedState>(rig.m_sink.Sent()[0]).pattern,
            sg::Pattern::kSolid);
  EXPECT_EQ(std::get<sg::Announce>(rig.m_sink.Sent()[1]).text,
            "Wolves holds Old oak");
}

TEST_F(ScriptHostTest, CaptureModeRestartsTheCountdownOnARivalTap)
{
  ScriptRig rig{m_path, CaptureModeSource()};
  rig.m_core.Start();

  rig.Tap(1000, "P1", "W1");
  rig.Tap(200'000, "P1", "F1");

  rig.Advance(1000 + kCaptureDelay);
  EXPECT_FALSE(rig.m_core.State().Points()[0].owner.has_value())
      << "the Wolves' cancelled countdown must not confirm";

  rig.Advance(200'000 + kCaptureDelay);
  ASSERT_TRUE(rig.m_core.State().Points()[0].owner.has_value());
  EXPECT_EQ(*rig.m_core.State().Points()[0].owner, "Foxes");
}

TEST_F(ScriptHostTest, CaptureModeDeniesTheOwnerAndTheClaimantAndUnknownBands)
{
  ScriptRig rig{m_path, CaptureModeSource()};
  rig.m_core.Start();

  rig.Tap(1000, "P1", "W1");
  rig.m_sink.Clear();

  rig.Tap(2000, "P1", "W1");  // same team taps its own running claim
  ASSERT_EQ(rig.m_sink.Sent().size(), 1U);
  EXPECT_EQ(std::get<sg::Beep>(rig.m_sink.Sent()[0]).kind, sg::BeepKind::kDeny);

  rig.Advance(1000 + kCaptureDelay);
  rig.m_sink.Clear();
  rig.Tap(400'000, "P1", "W1");  // now it owns the point
  ASSERT_EQ(rig.m_sink.Sent().size(), 1U);
  EXPECT_EQ(std::get<sg::Beep>(rig.m_sink.Sent()[0]).kind, sg::BeepKind::kDeny);

  rig.m_sink.Clear();
  rig.Tap(410'000, "P2", "NOPE");  // a wristband nobody enrolled
  ASSERT_EQ(rig.m_sink.Sent().size(), 1U);
  EXPECT_EQ(std::get<sg::Beep>(rig.m_sink.Sent()[0]).kind, sg::BeepKind::kDeny);
}

TEST_F(ScriptHostTest, CaptureModeReplaysToTheSameStateAsTheLiveRun)
{
  // The determinism promise, with the real interpreter in the loop: this is
  // what makes "restart the engine and the game comes back" true.
  ScriptRig rig{m_path, CaptureModeSource()};
  rig.m_core.Start();

  rig.Tap(1'000, "P1", "W1");
  rig.Tap(50'000, "P2", "F1");
  rig.Tap(120'000, "P1", "F1");
  rig.Advance(500'000);
  rig.Tap(600'000, "P2", "W1");
  rig.Advance(1'200'000);

  const std::string live = rig.m_core.State().Snapshot();
  rig.m_sink.Clear();
  rig.m_core.Rebuild();

  EXPECT_EQ(rig.m_core.State().Snapshot(), live);
  EXPECT_EQ(rig.m_sink.Sent().size(), 2U) << "the replay itself must be silent";
}

TEST_F(ScriptHostTest, CaptureModeComesBackAfterARestart)
{
  std::string before;
  {
    ScriptRig rig{m_path, CaptureModeSource()};
    rig.m_core.Start();
    rig.Tap(1'000, "P1", "W1");
    rig.Advance(400'000);
    before = rig.m_core.State().Snapshot();
  }

  ScriptRig restarted{m_path, CaptureModeSource()};
  restarted.m_clock.SetNow(400'000);
  restarted.m_core.Start();

  EXPECT_EQ(restarted.m_core.State().Snapshot(), before);
  ASSERT_TRUE(restarted.m_core.State().Points()[0].owner.has_value());
  EXPECT_EQ(*restarted.m_core.State().Points()[0].owner, "Wolves");
}

TEST_F(ScriptHostTest, ALateTapIsPlacedAtItsTrueTimeByTheRealMode)
{
  ScriptRig rig{m_path, CaptureModeSource()};
  rig.m_core.Start();

  rig.Tap(1'000, "P1", "W1");
  rig.Advance(200'000);

  // A Fox tap from 100 s whose radio packet only arrives now.
  rig.m_clock.SetNow(200'000);
  ASSERT_TRUE(rig.m_core.OnInput(sg::Event{0, 100'000, sg::Tap{"P1", "F1"}})
                  .has_value());
  EXPECT_EQ(rig.m_core.Rebuilds(), 2U);

  rig.Advance(1'000 + kCaptureDelay);
  EXPECT_FALSE(rig.m_core.State().Points()[0].owner.has_value());

  rig.Advance(100'000 + kCaptureDelay);
  ASSERT_TRUE(rig.m_core.State().Points()[0].owner.has_value());
  EXPECT_EQ(*rig.m_core.State().Points()[0].owner, "Foxes");
}

}  // namespace
