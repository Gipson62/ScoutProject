#include "engine/config/config.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>

#include "engine/config/roster.hpp"
#include "engine/core/error.hpp"
#include "engine/core/event.hpp"

namespace
{

class ConfigTest : public ::testing::Test
{
 protected:
  auto SetUp() -> void override
  {
    m_dir =
        std::filesystem::temp_directory_path() /
        ("sgengine-config-" +
         std::string{
             ::testing::UnitTest::GetInstance()->current_test_info()->name()});
    std::filesystem::remove_all(m_dir);
    std::filesystem::create_directories(m_dir);
    m_unused_path = m_dir / "nothing-here";
  }

  auto TearDown() -> void override
  {
    std::filesystem::remove_all(m_dir);
  }

  [[nodiscard]] auto Write(std::string_view name,
                           std::string_view content) const
      -> std::filesystem::path
  {
    const std::filesystem::path path = m_dir / name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
    return path;
  }

  // A path inside the temporary directory that no test creates.
  std::filesystem::path m_unused_path;

 private:
  std::filesystem::path m_dir;
};

constexpr std::string_view kGoodConfig = R"(mode  = "capture"
title = "Camp 2027, dev"
seed  = 2027
roster = "roster.csv"

[[teams]]
name = "Wolves"
colour = "#e8432e"

[[teams]]
name = "Foxes"
colour = "orange"

[params]
capture_minutes = 5
income_per_min  = 10

[points]
P2 = { name = "Stream", map_led = 1 }
P1 = { name = "Old oak", map_led = 0 }
)";

// --- ParseColour ---

TEST(ParseColour, AcceptsHexWithAndWithoutTheHash)
{
  const sg::Rgb expected{232, 67, 46};
  EXPECT_EQ(sg::ParseColour("#e8432e").value().r, expected.r);
  EXPECT_EQ(sg::ParseColour("E8432E").value().g, expected.g);
  EXPECT_EQ(sg::ParseColour("#e8432e").value().b, expected.b);
}

TEST(ParseColour, AcceptsTheNamesAChiefIsLikelyToType)
{
  EXPECT_EQ(sg::ParseColour("white").value().r, 255);
  EXPECT_EQ(sg::ParseColour("grey").value().g,
            sg::ParseColour("gray").value().g);
  EXPECT_TRUE(sg::ParseColour("orange").has_value());
}

TEST(ParseColour, RejectsWhatItCannotRead)
{
  EXPECT_EQ(sg::ParseColour("#12345").error().code,
            sg::ErrorCode::kConfigBadValue);
  EXPECT_EQ(sg::ParseColour("#gggggg").error().code,
            sg::ErrorCode::kConfigBadValue);
  EXPECT_EQ(sg::ParseColour("chartreuse").error().code,
            sg::ErrorCode::kConfigBadValue);
}

// --- LoadConfig ---

TEST_F(ConfigTest, ReadsEverythingTheCoreNeeds)
{
  const sg::Result<sg::Config> config =
      sg::LoadConfig(Write("dev.toml", kGoodConfig));
  ASSERT_TRUE(config.has_value()) << config.error().message;

  EXPECT_EQ(config->mode_path, std::filesystem::path{"modes"} / "capture.py");
  EXPECT_EQ(config->roster_path.filename(), "roster.csv");
  EXPECT_EQ(config->setup.title, "Camp 2027, dev");
  EXPECT_EQ(config->setup.seed, 2027U);

  ASSERT_EQ(config->setup.teams.size(), 2U);
  EXPECT_EQ(config->setup.teams[0].name, "Wolves");
  EXPECT_EQ(config->setup.teams[0].colour.r, 232);
  EXPECT_EQ(config->setup.teams[1].colour.r, 242)
      << "a named colour must resolve too";

  EXPECT_EQ(config->setup.params.at("capture_minutes").as_int64(), 5);
  // game.config is the whole file.
  EXPECT_EQ(
      config->setup.config.at("points").as_object().at("P1").as_object().at(
          "name"),
      "Old oak");
}

TEST_F(ConfigTest, SortsPointsByIdWhateverTheFileSays)
{
  // A TOML table has no order of its own, and point order shows up in the state
  // snapshot and in PushFullState, so it must not depend on how the file was
  // typed.
  const sg::Result<sg::Config> config =
      sg::LoadConfig(Write("dev.toml", kGoodConfig));
  ASSERT_TRUE(config.has_value()) << config.error().message;

  ASSERT_EQ(config->setup.points.size(), 2U);
  EXPECT_EQ(config->setup.points[0].id, "P1");
  EXPECT_EQ(config->setup.points[0].name, "Old oak");
  EXPECT_EQ(config->setup.points[0].map_led, 0);
  EXPECT_EQ(config->setup.points[1].id, "P2");
}

TEST_F(ConfigTest, AcceptsTheOtherTwoShapesTheDesignUses)
{
  // An inline teams array and a [nodes] table instead of [points]: the design
  // document writes it both ways.
  const sg::Result<sg::Config> config =
      sg::LoadConfig(Write("camp.toml", R"(mode = "domination"
teams = [ { name = "Wolves", colour = "#e8432e" }, { name = "Owls", colour = "#8e44ad" } ]

[nodes]
P1 = { name = "Old oak", yield = { gold = 15 } }
)"));
  ASSERT_TRUE(config.has_value()) << config.error().message;
  EXPECT_EQ(config->setup.teams.size(), 2U);
  ASSERT_EQ(config->setup.points.size(), 1U);
  EXPECT_EQ(config->setup.points[0].name, "Old oak");
  // The yield table is not the engine's business, but the script must see it.
  EXPECT_EQ(config->setup.config.at("nodes")
                .as_object()
                .at("P1")
                .as_object()
                .at("yield")
                .as_object()
                .at("gold")
                .as_int64(),
            15);
}

TEST_F(ConfigTest, DefaultsTheRosterPathToTheConfigsOwnDirectory)
{
  const std::filesystem::path path = Write("dev.toml", R"(mode = "capture"
[[teams]]
name = "Wolves"
[points]
P1 = {}
)");
  const sg::Result<sg::Config> config = sg::LoadConfig(path);
  ASSERT_TRUE(config.has_value()) << config.error().message;

  EXPECT_EQ(config->roster_path, path.parent_path() / "roster.csv");
  EXPECT_EQ(config->setup.points[0].name, "P1")
      << "a point with no name is named after its id";
  EXPECT_EQ(config->setup.points[0].map_led, -1);
  EXPECT_EQ(config->setup.teams[0].colour.r, 255)
      << "a team with no colour is white";
}

TEST_F(ConfigTest, NamesTheFileAndTheLineOfASyntaxError)
{
  const sg::Result<sg::Config> config =
      sg::LoadConfig(Write("broken.toml", R"(mode = "capture"
title = = "oops"
)"));
  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, sg::ErrorCode::kConfigParseFailed);
  EXPECT_NE(config.error().message.find("broken.toml:2:"), std::string::npos)
      << config.error().message;
}

TEST_F(ConfigTest, NamesTheLineOfABadColour)
{
  const sg::Result<sg::Config> config =
      sg::LoadConfig(Write("dev.toml", R"(mode = "capture"
[[teams]]
name = "Wolves"
colour = "chartreuse"
[points]
P1 = {}
)"));
  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, sg::ErrorCode::kConfigBadValue);
  EXPECT_NE(config.error().message.find("dev.toml:2:"), std::string::npos)
      << config.error().message;
  EXPECT_NE(config.error().message.find("Wolves"), std::string::npos)
      << config.error().message;
}

TEST_F(ConfigTest, ReportsWhatIsMissing)
{
  EXPECT_EQ(
      sg::LoadConfig(Write("a.toml", "title = \"no mode\"\n")).error().code,
      sg::ErrorCode::kConfigMissingField);
  EXPECT_EQ(
      sg::LoadConfig(Write("b.toml", "mode = \"capture\"\n")).error().code,
      sg::ErrorCode::kConfigMissingField);
  EXPECT_EQ(sg::LoadConfig(Write("c.toml", "mode = \"capture\"\nteams = []\n"))
                .error()
                .code,
            sg::ErrorCode::kConfigMissingField);
  EXPECT_EQ(sg::LoadConfig(m_unused_path).error().code,
            sg::ErrorCode::kConfigOpenFailed);
}

TEST_F(ConfigTest, RejectsANegativeSeed)
{
  const sg::Result<sg::Config> config =
      sg::LoadConfig(Write("dev.toml", R"(mode = "capture"
seed = -1
[[teams]]
name = "Wolves"
[points]
P1 = {}
)"));
  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, sg::ErrorCode::kConfigBadValue);
}

// --- CsvRoster ---

TEST_F(ConfigTest, RosterReadsWhatASpreadsheetExports)
{
  const sg::Result<sg::CsvRoster> roster =
      sg::CsvRoster::Load(Write("roster.csv",
                                "uid,player,team,role\r\n"
                                "04A1B2C3,Lucas,Wolves,player\r\n"
                                "04D4E5F6,Noah,Foxes,\r\n"
                                "\r\n"
                                "# a comment\r\n"
                                "0490ABCD,Bomb,Attack,bomb\r\n"));
  ASSERT_TRUE(roster.has_value()) << roster.error().message;
  EXPECT_EQ(roster->Size(), 3U);

  const sg::Tag lucas = roster->Lookup("04A1B2C3");
  EXPECT_TRUE(lucas.Known());
  EXPECT_EQ(lucas.team, "Wolves");
  EXPECT_EQ(lucas.player, "Lucas");
  EXPECT_EQ(lucas.role, "player");

  EXPECT_EQ(roster->Lookup("04D4E5F6").role, "player")
      << "an empty role defaults to player";
  EXPECT_EQ(roster->Lookup("0490ABCD").role, "bomb");
}

TEST_F(ConfigTest, RosterAcceptsTheMinimalTwoColumnForm)
{
  const sg::Result<sg::CsvRoster> roster = sg::CsvRoster::Load(
      Write("roster.csv", "uid,team\nW1,Wolves\nF1,Foxes\n"));
  ASSERT_TRUE(roster.has_value()) << roster.error().message;
  EXPECT_EQ(roster->Lookup("F1").team, "Foxes");
  EXPECT_TRUE(roster->Lookup("F1").player.empty());
}

TEST_F(ConfigTest, RosterAcceptsTagAsTheUidColumnName)
{
  const sg::Result<sg::CsvRoster> roster = sg::CsvRoster::Load(
      Write("roster.csv", "tag,player,team,role\n04A1,Lucas,Wolves,player\n"));
  ASSERT_TRUE(roster.has_value()) << roster.error().message;
  EXPECT_EQ(roster->Lookup("04A1").team, "Wolves");
}

TEST_F(ConfigTest, RosterTreatsAnUnknownWristbandAsUnknownNotAsAnError)
{
  const sg::Result<sg::CsvRoster> roster =
      sg::CsvRoster::Load(Write("roster.csv", "uid,team\nW1,Wolves\n"));
  ASSERT_TRUE(roster.has_value()) << roster.error().message;

  const sg::Tag unknown = roster->Lookup("NOPE");
  EXPECT_FALSE(unknown.Known());
  EXPECT_EQ(unknown.uid, "NOPE");
}

TEST_F(ConfigTest, RosterNamesTheFileAndLineOfEveryProblem)
{
  EXPECT_NE(sg::CsvRoster::Load(Write("a.csv", "uid,team\nW1,\n"))
                .error()
                .message.find("a.csv:2:"),
            std::string::npos);
  EXPECT_EQ(
      sg::CsvRoster::Load(Write("b.csv", "uid,team\n,Wolves\n")).error().code,
      sg::ErrorCode::kRosterParseFailed);
  EXPECT_EQ(sg::CsvRoster::Load(Write("c.csv", "player,role\nLucas,player\n"))
                .error()
                .code,
            sg::ErrorCode::kRosterParseFailed);
  EXPECT_EQ(sg::CsvRoster::Load(Write("d.csv", "")).error().code,
            sg::ErrorCode::kRosterParseFailed);
  EXPECT_EQ(sg::CsvRoster::Load(m_unused_path).error().code,
            sg::ErrorCode::kRosterOpenFailed);
}

TEST_F(ConfigTest, RosterRejectsAWristbandEnrolledTwice)
{
  const sg::Result<sg::CsvRoster> roster = sg::CsvRoster::Load(
      Write("roster.csv", "uid,team\nW1,Wolves\nW1,Foxes\n"));
  ASSERT_FALSE(roster.has_value());
  EXPECT_NE(roster.error().message.find("already enrolled for Wolves"),
            std::string::npos)
      << roster.error().message;
}

}  // namespace
