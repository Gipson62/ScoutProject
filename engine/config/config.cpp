#include "engine/config/config.hpp"

#include <array>
#include <boost/json.hpp>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>
#include <utility>
#include <vector>

#include "engine/core/error.hpp"
#include "engine/core/event.hpp"
#include "engine/core/game_state.hpp"

namespace sg
{
namespace
{

// A chief writes a colour by name as often as by hex, and DESIGN.md's own two
// config examples use one each.
constexpr std::array<std::pair<std::string_view, Rgb>, 14> kNamedColours{{
    {"white", {255, 255, 255}},
    {"black", {0, 0, 0}},
    {"grey", {128, 128, 128}},
    {"gray", {128, 128, 128}},
    {"red", {232, 67, 46}},
    {"orange", {242, 140, 40}},
    {"yellow", {235, 203, 40}},
    {"green", {46, 158, 68}},
    {"blue", {43, 108, 212}},
    {"purple", {142, 68, 173}},
    {"brown", {109, 76, 65}},
    {"pink", {235, 110, 170}},
    {"cyan", {40, 200, 210}},
    {"magenta", {210, 40, 180}},
}};

[[nodiscard]] auto HexDigit(char c) -> Result<std::uint8_t>
{
  if (c >= '0' && c <= '9')
  {
    return static_cast<std::uint8_t>(c - '0');
  }
  if (c >= 'a' && c <= 'f')
  {
    return static_cast<std::uint8_t>(c - 'a' + 10);
  }
  if (c >= 'A' && c <= 'F')
  {
    return static_cast<std::uint8_t>(c - 'A' + 10);
  }
  return Fail(ErrorCode::kConfigBadValue, std::string{"not a hex digit: "} + c);
}

// Where an error happened, in the form an editor can jump to.
[[nodiscard]] auto At(const std::filesystem::path& path, const toml::node& node)
    -> std::string
{
  const toml::source_region& where = node.source();
  return path.string() + ":" + std::to_string(where.begin.line) + ":" +
         std::to_string(where.begin.column) + ": ";
}

[[nodiscard]] auto TomlToJson(const toml::node& node) -> boost::json::value
{
  if (const toml::table* table = node.as_table())
  {
    boost::json::object out;
    for (const auto& [key, value] : *table)
    {
      out[std::string{key.str()}] = TomlToJson(value);
    }
    return out;
  }
  if (const toml::array* array = node.as_array())
  {
    boost::json::array out;
    for (const toml::node& element : *array)
    {
      out.push_back(TomlToJson(element));
    }
    return out;
  }
  if (const auto* text = node.as_string())
  {
    return boost::json::string{text->get()};
  }
  if (const auto* integer = node.as_integer())
  {
    return integer->get();
  }
  if (const auto* number = node.as_floating_point())
  {
    return number->get();
  }
  if (const auto* flag = node.as_boolean())
  {
    return flag->get();
  }
  // Dates and times have no JSON form; a mode script reads them as text.
  std::ostringstream text;
  if (const auto* date = node.as_date())
  {
    text << date->get();
  }
  else if (const auto* time = node.as_time())
  {
    text << time->get();
  }
  else if (const auto* date_time = node.as_date_time())
  {
    text << date_time->get();
  }
  else
  {
    return boost::json::value{};
  }
  return boost::json::string{text.str()};
}

[[nodiscard]] auto ReadTeams(const std::filesystem::path& path,
                             const toml::table& root)
    -> Result<std::vector<TeamSpec>>
{
  const toml::node* node = root.get("teams");
  if (node == nullptr)
  {
    return Fail(ErrorCode::kConfigMissingField, path.string() + ": no teams");
  }
  const toml::array* array = node->as_array();
  if (array == nullptr)
  {
    return Fail(ErrorCode::kConfigBadValue,
                At(path, *node) + "teams is not an array of tables");
  }

  std::vector<TeamSpec> teams;
  for (const toml::node& element : *array)
  {
    const toml::table* table = element.as_table();
    if (table == nullptr)
    {
      return Fail(ErrorCode::kConfigBadValue,
                  At(path, element) + "a team is not a table");
    }
    const std::optional<std::string> name =
        (*table)["name"].value<std::string>();
    if (!name.has_value() || name->empty())
    {
      return Fail(ErrorCode::kConfigMissingField,
                  At(path, element) + "a team has no name");
    }

    const std::string colour_text =
        (*table)["colour"].value_or(std::string{"white"});
    const Result<Rgb> colour = ParseColour(colour_text);
    if (!colour)
    {
      return Fail(
          ErrorCode::kConfigBadValue,
          At(path, element) + "team " + *name + ": " + colour.error().message);
    }
    teams.push_back(TeamSpec{*name, *colour});
  }

  if (teams.empty())
  {
    return Fail(ErrorCode::kConfigMissingField,
                path.string() + ": teams is empty");
  }
  return teams;
}

[[nodiscard]] auto ReadPoints(const std::filesystem::path& path,
                              const toml::table& root)
    -> Result<std::vector<PointSpec>>
{
  // DESIGN.md calls this table `points` in one place and `nodes` in another, so
  // both are accepted.
  const toml::node* node = root.get("points");
  if (node == nullptr)
  {
    node = root.get("nodes");
  }
  if (node == nullptr)
  {
    return Fail(ErrorCode::kConfigMissingField, path.string() + ": no points");
  }
  const toml::table* table = node->as_table();
  if (table == nullptr)
  {
    return Fail(ErrorCode::kConfigBadValue,
                At(path, *node) + "points is not a table");
  }

  std::vector<PointSpec> points;
  for (const auto& [key, value] : *table)
  {
    const toml::table* entry = value.as_table();
    if (entry == nullptr)
    {
      return Fail(ErrorCode::kConfigBadValue, At(path, value) + "point " +
                                                  std::string{key.str()} +
                                                  " is not a table");
    }
    PointSpec spec;
    spec.id = std::string{key.str()};
    spec.name = (*entry)["name"].value_or(spec.id);

    const std::int64_t map_led = (*entry)["map_led"].value_or(std::int64_t{-1});
    if (map_led < -1 || map_led > INT32_MAX)
    {
      return Fail(
          ErrorCode::kConfigBadValue,
          At(path, value) + "point " + spec.id + ": map_led is out of range");
    }
    spec.map_led = static_cast<std::int32_t>(map_led);
    points.push_back(std::move(spec));
  }

  if (points.empty())
  {
    return Fail(ErrorCode::kConfigMissingField,
                path.string() + ": points is empty");
  }
  // A TOML table keeps no order of its own, so the points are sorted by id:
  // point order is part of the replayed state, through PushFullState and the
  // state snapshot, and must not depend on how the file was typed.
  std::ranges::sort(points,
                    [](const PointSpec& a, const PointSpec& b)
                    {
                      return a.id < b.id;
                    });
  return points;
}

}  // namespace

auto ParseColour(std::string_view text) -> Result<Rgb>
{
  for (const auto& [name, colour] : kNamedColours)
  {
    if (text == name)
    {
      return colour;
    }
  }

  std::string_view digits = text;
  if (digits.starts_with('#'))
  {
    digits.remove_prefix(1);
  }
  if (digits.size() != 6)
  {
    return Fail(ErrorCode::kConfigBadValue,
                "not a colour name and not #rrggbb: " + std::string{text});
  }

  Rgb rgb;
  const std::array<std::uint8_t*, 3> channels{&rgb.r, &rgb.g, &rgb.b};
  for (std::size_t i = 0; i < channels.size(); ++i)
  {
    const Result<std::uint8_t> high = HexDigit(digits[i * 2]);
    const Result<std::uint8_t> low = HexDigit(digits[(i * 2) + 1]);
    if (!high)
    {
      return std::unexpected(high.error());
    }
    if (!low)
    {
      return std::unexpected(low.error());
    }
    *channels[i] = static_cast<std::uint8_t>((*high * 16) + *low);
  }
  return rgb;
}

auto LoadConfig(const std::filesystem::path& path) -> Result<Config>
{
  if (!std::filesystem::exists(path))
  {
    return Fail(ErrorCode::kConfigOpenFailed,
                "no such config file: " + path.string());
  }

  // TOML_EXCEPTIONS is 0, so a bad document comes back as a result to check.
  const toml::parse_result parsed = toml::parse_file(path.string());
  if (!parsed)
  {
    const toml::parse_error& error = parsed.error();
    return Fail(ErrorCode::kConfigParseFailed,
                path.string() + ":" +
                    std::to_string(error.source().begin.line) + ":" +
                    std::to_string(error.source().begin.column) + ": " +
                    std::string{error.description()});
  }
  const toml::table& root = parsed.table();

  Config config;
  const std::string mode = root["mode"].value_or(std::string{});
  if (mode.empty())
  {
    return Fail(ErrorCode::kConfigMissingField, path.string() + ": no mode");
  }
  config.mode_path = std::filesystem::path{"modes"} / (mode + ".py");

  // A relative roster path is relative to the config file, not to wherever the
  // engine was started from.
  const std::filesystem::path base =
      path.has_parent_path() ? path.parent_path() : ".";
  const std::filesystem::path roster =
      root["roster"].value_or(std::string{"roster.csv"});
  config.roster_path = roster.is_absolute() ? roster : base / roster;

  config.setup.title = root["title"].value_or(std::string{"untitled game"});

  const std::int64_t seed = root["seed"].value_or(std::int64_t{0});
  if (seed < 0)
  {
    return Fail(ErrorCode::kConfigBadValue,
                path.string() + ": seed must not be negative");
  }
  config.setup.seed = static_cast<std::uint64_t>(seed);

  Result<std::vector<TeamSpec>> teams = ReadTeams(path, root);
  if (!teams)
  {
    return std::unexpected(teams.error());
  }
  config.setup.teams = std::move(*teams);

  Result<std::vector<PointSpec>> points = ReadPoints(path, root);
  if (!points)
  {
    return std::unexpected(points.error());
  }
  config.setup.points = std::move(*points);

  if (const toml::node* params = root.get("params"))
  {
    const boost::json::value converted = TomlToJson(*params);
    if (const boost::json::object* object = converted.if_object())
    {
      config.setup.params = *object;
    }
    else
    {
      return Fail(ErrorCode::kConfigBadValue,
                  At(path, *params) + "params is not a table");
    }
  }

  // The whole file, read-only, for the mode script's game.config.
  const boost::json::value whole = TomlToJson(root);
  config.setup.config = whole.get_object();

  return config;
}

}  // namespace sg
