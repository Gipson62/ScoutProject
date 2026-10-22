#include "engine/io/state_frame.hpp"

#include <gtest/gtest.h>

#include <boost/json.hpp>
#include <string>
#include <vector>

#include "engine/core/event.hpp"
#include "engine/core/game_state.hpp"
#include "engine/core/mode.hpp"

namespace
{

[[nodiscard]] auto MakeState() -> sg::GameState
{
  sg::Setup setup;
  setup.seed = 2027;
  setup.teams = {{"Wolves", {232, 67, 46}}, {"Foxes", {242, 140, 40}}};
  setup.points = {{"P1", "Old oak", 0}, {"P2", "Stream", 3}};
  return sg::GameState::Initial(setup);
}

TEST(StateFrame, CarriesWhatAPageNeedsToDrawItself)
{
  sg::GameState state = MakeState();
  state.Points()[0].owner = "Wolves";
  state.Points()[0].led =
      sg::LedState{"P1", {232, 67, 46}, sg::Pattern::kSolid, 4};
  state.Teams()[0].resources["wood"] = 30;

  const std::vector<sg::Tag> bands{{"W1", "Wolves", "Lucas", "player"},
                                   {"F1", "Foxes", "Noah", "player"}};
  const boost::json::object frame = sg::StateFrame(state, 61'000, bands);

  EXPECT_EQ(frame.at("type").as_string(), "state");
  EXPECT_EQ(frame.at("now").as_int64(), 61'000);

  const boost::json::array& teams = frame.at("teams").as_array();
  ASSERT_EQ(teams.size(), 2U);
  EXPECT_EQ(teams.at(0).as_object().at("name").as_string(), "Wolves");
  EXPECT_EQ(teams.at(0).as_object().at("rgb").as_array().at(0).as_int64(), 232);
  EXPECT_EQ(teams.at(0).as_object().at("res").as_object().at("wood").as_int64(),
            30);

  const boost::json::array& points = frame.at("points").as_array();
  ASSERT_EQ(points.size(), 2U);
  EXPECT_EQ(points.at(0).as_object().at("id").as_string(), "P1");
  EXPECT_EQ(points.at(0).as_object().at("name").as_string(), "Old oak");
  EXPECT_EQ(points.at(0).as_object().at("owner").as_string(), "Wolves");
  EXPECT_EQ(points.at(0).as_object().at("pattern").as_string(), "solid");
  EXPECT_EQ(points.at(0).as_object().at("v").as_int64(), 4);
  EXPECT_EQ(points.at(0).as_object().at("map_led").as_int64(), 0);

  // A point nobody holds says so, rather than leaving the key out: a page that
  // has to guess shows the wrong thing.
  EXPECT_TRUE(points.at(1).as_object().at("owner").is_null());

  const boost::json::array& bands_out = frame.at("wristbands").as_array();
  ASSERT_EQ(bands_out.size(), 2U);
  EXPECT_EQ(bands_out.at(0).as_object().at("uid").as_string(), "W1");
  EXPECT_EQ(bands_out.at(0).as_object().at("team").as_string(), "Wolves");
  EXPECT_EQ(bands_out.at(0).as_object().at("player").as_string(), "Lucas");
}

TEST(StateFrame, WorksWithNoWristbandsAtAll)
{
  const sg::GameState state = MakeState();
  const boost::json::object frame = sg::StateFrame(state, 0, {});
  EXPECT_TRUE(frame.at("wristbands").as_array().empty());
}

TEST(EventFrame, KeepsTheKindAndBecomesAFrame)
{
  const sg::Event event{7, 1234, sg::Tap{"P3", "04A1B2C3"}};
  const boost::json::object frame = sg::EventFrame(event);

  EXPECT_EQ(frame.at("type").as_string(), "event");
  EXPECT_EQ(frame.at("kind").as_string(), "tap");
  EXPECT_EQ(frame.at("seq").as_int64(), 7);
  EXPECT_EQ(frame.at("t").as_int64(), 1234);
  EXPECT_EQ(frame.at("point").as_string(), "P3");
}

}  // namespace
