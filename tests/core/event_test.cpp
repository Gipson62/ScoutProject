#include "engine/core/event.hpp"

#include <gtest/gtest.h>

#include <boost/json.hpp>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "engine/core/error.hpp"

namespace
{

[[nodiscard]] auto Parse(std::string_view text) -> boost::json::object
{
  boost::system::error_code ec;
  const boost::json::value value = boost::json::parse(text, ec);
  EXPECT_FALSE(ec) << ec.message();
  return value.as_object();
}

[[nodiscard]] auto Serialize(const boost::json::object& object) -> std::string
{
  return boost::json::serialize(object);
}

// --- names ---

TEST(Names, PatternRoundTripsThroughItsName)
{
  for (const sg::Pattern pattern : {sg::Pattern::kOff, sg::Pattern::kSolid,
                                    sg::Pattern::kPulse, sg::Pattern::kBlink})
  {
    EXPECT_EQ(sg::PatternFromName(sg::PatternName(pattern)).value(), pattern);
  }
  EXPECT_EQ(sg::PatternFromName("slow").error().code,
            sg::ErrorCode::kUnknownKind);
}

TEST(Names, BeepKindRoundTripsThroughItsName)
{
  for (const sg::BeepKind kind :
       {sg::BeepKind::kOk, sg::BeepKind::kDeny, sg::BeepKind::kAlarm})
  {
    EXPECT_EQ(sg::BeepKindFromName(sg::BeepKindName(kind)).value(), kind);
  }
  EXPECT_EQ(sg::BeepKindFromName("buzz").error().code,
            sg::ErrorCode::kUnknownKind);
}

// --- events ---

TEST(EventJson, WritesTheShapeTheLogFileHolds)
{
  const sg::Event tap{7, 1234, sg::Tap{"P3", "04A1B2C3"}};
  EXPECT_EQ(Serialize(sg::ToJson(tap)),
            R"({"seq":7,"t":1234,"kind":"tap","point":"P3","uid":"04A1B2C3"})");

  const sg::Event undo{9, 5000, sg::Void{7}};
  EXPECT_EQ(Serialize(sg::ToJson(undo)),
            R"({"seq":9,"t":5000,"kind":"void","target":7})");
}

TEST(EventJson, RoundTripsEveryKind)
{
  boost::json::object args;
  args["target"] = "D5";
  args["force"] = boost::json::object{{"archers", 4}};

  const std::vector<sg::Event> events{
      {1, 10, sg::Tap{"P1", "04A1B2C3"}},
      {2, 20, sg::Button{"P2"}},
      {3, 30, sg::Action{"Wolves", "attack", args}},
      {4, 40, sg::Admin{"raid", args}},
      {5, 50, sg::Void{3}},
  };

  for (const sg::Event& event : events)
  {
    const sg::Result<sg::Event> back = sg::EventFromJson(sg::ToJson(event));
    ASSERT_TRUE(back.has_value()) << back.error().message;
    EXPECT_EQ(back->seq, event.seq);
    EXPECT_EQ(back->t, event.t);
    EXPECT_EQ(back->data.index(), event.data.index());
    EXPECT_EQ(Serialize(sg::ToJson(*back)), Serialize(sg::ToJson(event)));
  }
}

TEST(EventJson, ReadsAnEventWithoutArgsAsAnEmptyObject)
{
  const sg::Result<sg::Event> event = sg::EventFromJson(
      Parse(R"({"seq":1,"t":2,"kind":"admin","name":"pause"})"));
  ASSERT_TRUE(event.has_value()) << event.error().message;
  EXPECT_TRUE(std::get<sg::Admin>(event->data).args.empty());
}

TEST(EventJson, NamesTheFieldThatIsMissingOrWrong)
{
  const sg::Result<sg::Event> no_uid =
      sg::EventFromJson(Parse(R"({"seq":1,"t":2,"kind":"tap","point":"P1"})"));
  ASSERT_FALSE(no_uid.has_value());
  EXPECT_EQ(no_uid.error().code, sg::ErrorCode::kMissingField);
  EXPECT_EQ(no_uid.error().message, "uid");

  const sg::Result<sg::Event> point_is_a_number = sg::EventFromJson(
      Parse(R"({"seq":1,"t":2,"kind":"tap","point":3,"uid":"04"})"));
  ASSERT_FALSE(point_is_a_number.has_value());
  EXPECT_EQ(point_is_a_number.error().code, sg::ErrorCode::kWrongType);
  EXPECT_EQ(point_is_a_number.error().message, "point is not a string");

  const sg::Result<sg::Event> no_seq =
      sg::EventFromJson(Parse(R"({"t":2,"kind":"button"})"));
  ASSERT_FALSE(no_seq.has_value());
  EXPECT_EQ(no_seq.error().message, "seq");

  const sg::Result<sg::Event> strange =
      sg::EventFromJson(Parse(R"({"seq":1,"t":2,"kind":"sneeze"})"));
  ASSERT_FALSE(strange.has_value());
  EXPECT_EQ(strange.error().code, sg::ErrorCode::kUnknownKind);
}

TEST(EventJson, AcceptsANegativeTimestamp)
{
  // A late radio packet can be back-dated before the engine's own start.
  const sg::Result<sg::Event> event = sg::EventFromJson(
      Parse(R"({"seq":1,"t":-250,"kind":"button","point":"P4"})"));
  ASSERT_TRUE(event.has_value()) << event.error().message;
  EXPECT_EQ(event->t, -250);
}

// --- commands ---

TEST(CommandJson, WritesTheFramesTheDesignLists)
{
  EXPECT_EQ(
      Serialize(sg::ToJson(sg::Command{
          sg::LedState{"P3", {255, 120, 0}, sg::Pattern::kPulse, 12}})),
      R"({"type":"led","point":"P3","rgb":[255,120,0],"pattern":"pulse","v":12})");

  boost::json::object view;
  view["sections"] = boost::json::array{};
  EXPECT_EQ(Serialize(sg::ToJson(sg::Command{sg::PanelPush{"Wolves", view}})),
            R"({"type":"panel","team":"Wolves","sections":[]})");
}

TEST(CommandJson, RoundTripsEveryType)
{
  boost::json::object view;
  view["sections"] =
      boost::json::array{boost::json::object{{"title", "Resources"}}};

  const std::vector<sg::Command> commands{
      sg::LedState{"P1", {1, 2, 3}, sg::Pattern::kSolid, 65535},
      sg::Beep{"P2", sg::BeepKind::kDeny},
      sg::Sound{"turn.wav"},
      sg::MapLed{119, {0, 0, 0}, sg::Pattern::kOff},
      sg::PanelPush{"Foxes", view},
      sg::Announce{"Turn 2 begins", "all"},
  };

  for (const sg::Command& command : commands)
  {
    const sg::Result<sg::Command> back =
        sg::CommandFromJson(sg::ToJson(command));
    ASSERT_TRUE(back.has_value()) << back.error().message;
    EXPECT_EQ(back->index(), command.index());
    EXPECT_EQ(Serialize(sg::ToJson(*back)), Serialize(sg::ToJson(command)));
  }
}

TEST(CommandJson, RejectsAColourChannelOutsideAByte)
{
  const sg::Result<sg::Command> too_bright = sg::CommandFromJson(Parse(
      R"({"type":"led","point":"P1","rgb":[300,0,0],"pattern":"solid","v":1})"));
  ASSERT_FALSE(too_bright.has_value());
  EXPECT_EQ(too_bright.error().code, sg::ErrorCode::kBadValue);

  const sg::Result<sg::Command> two_channels = sg::CommandFromJson(Parse(
      R"({"type":"led","point":"P1","rgb":[1,2],"pattern":"solid","v":1})"));
  ASSERT_FALSE(two_channels.has_value());
  EXPECT_EQ(two_channels.error().code, sg::ErrorCode::kWrongType);
}

TEST(CommandJson, RejectsAnLedVersionAboveSixteenBits)
{
  const sg::Result<sg::Command> command = sg::CommandFromJson(Parse(
      R"({"type":"led","point":"P1","rgb":[0,0,0],"pattern":"off","v":65536})"));
  ASSERT_FALSE(command.has_value());
  EXPECT_EQ(command.error().code, sg::ErrorCode::kBadValue);
}

TEST(CommandJson, RejectsAnUnknownType)
{
  const sg::Result<sg::Command> command =
      sg::CommandFromJson(Parse(R"({"type":"smoke"})"));
  ASSERT_FALSE(command.has_value());
  EXPECT_EQ(command.error().code, sg::ErrorCode::kUnknownKind);
  EXPECT_EQ(command.error().message, "unknown command type: smoke");
}

}  // namespace
