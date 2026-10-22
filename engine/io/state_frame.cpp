#include "engine/io/state_frame.hpp"

#include <boost/json.hpp>
#include <cstdint>
#include <span>
#include <string>

#include "engine/core/event.hpp"
#include "engine/core/game_state.hpp"
#include "engine/core/mode.hpp"

namespace sg
{
namespace
{

// Signed throughout, so a frame built here and a frame parsed back from text
// hold the same JSON kinds.
[[nodiscard]] auto RgbArray(const Rgb& rgb) -> boost::json::array
{
  return boost::json::array{static_cast<std::int64_t>(rgb.r),
                            static_cast<std::int64_t>(rgb.g),
                            static_cast<std::int64_t>(rgb.b)};
}

}  // namespace

auto StateFrame(const GameState& state, Millis now,
                std::span<const Tag> wristbands) -> boost::json::object
{
  boost::json::object frame;
  frame["type"] = "state";
  frame["now"] = now;

  boost::json::array teams;
  for (const TeamState& team : state.Teams())
  {
    boost::json::object out;
    out["name"] = team.name;
    out["rgb"] = RgbArray(team.colour);
    boost::json::object resources;
    for (const auto& [name, count] : team.resources)
    {
      resources[name] = count;
    }
    out["res"] = resources;
    teams.push_back(out);
  }
  frame["teams"] = teams;

  boost::json::array points;
  for (const PointState& point : state.Points())
  {
    boost::json::object out;
    out["id"] = point.id;
    out["name"] = point.name;
    out["map_led"] = point.map_led;
    if (point.owner.has_value())
    {
      out["owner"] = *point.owner;
    }
    else
    {
      out["owner"] = nullptr;
    }
    out["rgb"] = RgbArray(point.led.rgb);
    out["pattern"] = PatternName(point.led.pattern);
    out["v"] = static_cast<std::int64_t>(point.led.version);
    points.push_back(out);
  }
  frame["points"] = points;

  boost::json::array bands;
  for (const Tag& tag : wristbands)
  {
    boost::json::object out;
    out["uid"] = tag.uid;
    out["team"] = tag.team;
    out["player"] = tag.player;
    out["role"] = tag.role;
    bands.push_back(out);
  }
  frame["wristbands"] = bands;

  return frame;
}

auto EventFrame(const Event& event) -> boost::json::object
{
  boost::json::object frame = ToJson(event);
  // The log line calls it "kind"; a frame calls its own discriminator "type",
  // so the kind travels under a second name and the frame stays a frame.
  frame["type"] = "event";
  return frame;
}

}  // namespace sg
