#include "engine/core/event.hpp"

#include <array>
#include <boost/json.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "engine/core/compiler.hpp"
#include "engine/core/error.hpp"

namespace sg
{
namespace
{

// Every read below uses the non-throwing Boost.JSON API (if_contains, if_*),
// because a throwing call in a build without an unwinder is a crash.

[[nodiscard]] auto RequireString(const boost::json::object& object,
                                 std::string_view key) -> Result<std::string>
{
  const boost::json::value* value = object.if_contains(key);
  if (value == nullptr)
  {
    return Fail(ErrorCode::kMissingField, std::string{key});
  }
  const boost::json::string* text = value->if_string();
  if (text == nullptr)
  {
    return Fail(ErrorCode::kWrongType, std::string{key} + " is not a string");
  }
  return std::string{text->c_str(), text->size()};
}

[[nodiscard]] auto RequireInteger(const boost::json::object& object,
                                  std::string_view key) -> Result<std::int64_t>
{
  const boost::json::value* value = object.if_contains(key);
  if (value == nullptr)
  {
    return Fail(ErrorCode::kMissingField, std::string{key});
  }
  boost::system::error_code ec;
  const std::int64_t number = value->to_number<std::int64_t>(ec);
  if (ec)
  {
    return Fail(ErrorCode::kWrongType, std::string{key} + " is not an integer");
  }
  return number;
}

[[nodiscard]] auto RequireInRange(std::string_view key, std::int64_t value,
                                  std::int64_t low, std::int64_t high)
    -> Result<std::int64_t>
{
  if (value < low || value > high)
  {
    return Fail(ErrorCode::kBadValue, std::string{key} + " is out of range: " +
                                          std::to_string(value));
  }
  return value;
}

// An absent args or view object is read as an empty one: a hook that takes no
// argument should not have to send "args": {}.
[[nodiscard]] auto ObjectOrEmpty(const boost::json::object& object,
                                 std::string_view key)
    -> Result<boost::json::object>
{
  const boost::json::value* value = object.if_contains(key);
  if (value == nullptr)
  {
    return boost::json::object{};
  }
  const boost::json::object* nested = value->if_object();
  if (nested == nullptr)
  {
    return Fail(ErrorCode::kWrongType, std::string{key} + " is not an object");
  }
  return *nested;
}

[[nodiscard]] auto RgbToJson(const Rgb& rgb) -> boost::json::array
{
  return boost::json::array{rgb.r, rgb.g, rgb.b};
}

[[nodiscard]] auto RgbFromJson(const boost::json::object& object) -> Result<Rgb>
{
  const boost::json::value* value = object.if_contains("rgb");
  if (value == nullptr)
  {
    return Fail(ErrorCode::kMissingField, "rgb");
  }
  const boost::json::array* channels = value->if_array();
  if (channels == nullptr || channels->size() != 3)
  {
    return Fail(ErrorCode::kWrongType, "rgb is not an array of three numbers");
  }

  Rgb rgb;
  const std::array<std::uint8_t*, 3> out{&rgb.r, &rgb.g, &rgb.b};
  for (std::size_t i = 0; i < out.size(); ++i)
  {
    boost::system::error_code ec;
    const std::int64_t channel = (*channels)[i].to_number<std::int64_t>(ec);
    if (ec)
    {
      return Fail(ErrorCode::kWrongType,
                  "rgb holds a value that is not a number");
    }
    const Result<std::int64_t> checked = RequireInRange("rgb", channel, 0, 255);
    if (!checked)
    {
      return std::unexpected(checked.error());
    }
    *out[i] = static_cast<std::uint8_t>(*checked);
  }
  return rgb;
}

[[nodiscard]] auto PatternFromJson(const boost::json::object& object)
    -> Result<Pattern>
{
  return RequireString(object, "pattern")
      .and_then(
          [](const std::string& name)
          {
            return PatternFromName(name);
          });
}

}  // namespace

auto PatternName(Pattern pattern) -> std::string_view
{
  switch (pattern)
  {
    case Pattern::kOff:
      return "off";
    case Pattern::kSolid:
      return "solid";
    case Pattern::kPulse:
      return "pulse";
    case Pattern::kBlink:
      return "blink";
  }
  Unreachable();
}

auto PatternFromName(std::string_view name) -> Result<Pattern>
{
  if (name == "off")
  {
    return Pattern::kOff;
  }
  if (name == "solid")
  {
    return Pattern::kSolid;
  }
  if (name == "pulse")
  {
    return Pattern::kPulse;
  }
  if (name == "blink")
  {
    return Pattern::kBlink;
  }
  return Fail(ErrorCode::kUnknownKind,
              "unknown LED pattern: " + std::string{name});
}

auto BeepKindName(BeepKind kind) -> std::string_view
{
  switch (kind)
  {
    case BeepKind::kOk:
      return "ok";
    case BeepKind::kDeny:
      return "deny";
    case BeepKind::kAlarm:
      return "alarm";
  }
  Unreachable();
}

auto BeepKindFromName(std::string_view name) -> Result<BeepKind>
{
  if (name == "ok")
  {
    return BeepKind::kOk;
  }
  if (name == "deny")
  {
    return BeepKind::kDeny;
  }
  if (name == "alarm")
  {
    return BeepKind::kAlarm;
  }
  return Fail(ErrorCode::kUnknownKind,
              "unknown beep kind: " + std::string{name});
}

auto ToJson(const Event& event) -> boost::json::object
{
  boost::json::object out;
  // Written as signed numbers so a frame built here and a frame parsed back
  // from a log line hold the same JSON kinds and compare equal.
  out["seq"] = static_cast<std::int64_t>(event.seq);
  out["t"] = event.t;
  std::visit(
      [&out](const auto& data)
      {
        using T = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<T, Tap>)
        {
          out["kind"] = "tap";
          out["point"] = data.point;
          out["uid"] = data.uid;
        }
        else if constexpr (std::is_same_v<T, Button>)
        {
          out["kind"] = "button";
          out["point"] = data.point;
        }
        else if constexpr (std::is_same_v<T, Action>)
        {
          out["kind"] = "action";
          out["team"] = data.team;
          out["name"] = data.name;
          out["args"] = data.args;
        }
        else if constexpr (std::is_same_v<T, Admin>)
        {
          out["kind"] = "admin";
          out["name"] = data.name;
          out["args"] = data.args;
        }
        else
        {
          static_assert(std::is_same_v<T, Void>);
          out["kind"] = "void";
          out["target"] = static_cast<std::int64_t>(data.target);
        }
      },
      event.data);
  return out;
}

auto EventFromJson(const boost::json::object& object) -> Result<Event>
{
  Event event;

  // seq and t are written by the log, but a line can predate them, so both are
  // required: a replay that guessed a timestamp would not be a replay.
  const Result<std::int64_t> seq = RequireInteger(object, "seq");
  if (!seq)
  {
    return std::unexpected(seq.error());
  }
  const Result<std::int64_t> checked_seq =
      RequireInRange("seq", *seq, 0, INT64_MAX);
  if (!checked_seq)
  {
    return std::unexpected(checked_seq.error());
  }
  event.seq = static_cast<std::uint64_t>(*checked_seq);

  const Result<std::int64_t> t = RequireInteger(object, "t");
  if (!t)
  {
    return std::unexpected(t.error());
  }
  event.t = *t;

  const Result<std::string> kind = RequireString(object, "kind");
  if (!kind)
  {
    return std::unexpected(kind.error());
  }

  if (*kind == "tap")
  {
    const Result<std::string> point = RequireString(object, "point");
    const Result<std::string> uid = RequireString(object, "uid");
    if (!point)
    {
      return std::unexpected(point.error());
    }
    if (!uid)
    {
      return std::unexpected(uid.error());
    }
    event.data = Tap{*point, *uid};
    return event;
  }
  if (*kind == "button")
  {
    const Result<std::string> point = RequireString(object, "point");
    if (!point)
    {
      return std::unexpected(point.error());
    }
    event.data = Button{*point};
    return event;
  }
  if (*kind == "action")
  {
    const Result<std::string> team = RequireString(object, "team");
    const Result<std::string> name = RequireString(object, "name");
    const Result<boost::json::object> args = ObjectOrEmpty(object, "args");
    if (!team)
    {
      return std::unexpected(team.error());
    }
    if (!name)
    {
      return std::unexpected(name.error());
    }
    if (!args)
    {
      return std::unexpected(args.error());
    }
    event.data = Action{*team, *name, *args};
    return event;
  }
  if (*kind == "admin")
  {
    const Result<std::string> name = RequireString(object, "name");
    const Result<boost::json::object> args = ObjectOrEmpty(object, "args");
    if (!name)
    {
      return std::unexpected(name.error());
    }
    if (!args)
    {
      return std::unexpected(args.error());
    }
    event.data = Admin{*name, *args};
    return event;
  }
  if (*kind == "void")
  {
    const Result<std::int64_t> target = RequireInteger(object, "target");
    if (!target)
    {
      return std::unexpected(target.error());
    }
    const Result<std::int64_t> checked =
        RequireInRange("target", *target, 0, INT64_MAX);
    if (!checked)
    {
      return std::unexpected(checked.error());
    }
    event.data = Void{static_cast<std::uint64_t>(*checked)};
    return event;
  }
  return Fail(ErrorCode::kUnknownKind, "unknown event kind: " + *kind);
}

auto ToJson(const Command& command) -> boost::json::object
{
  boost::json::object out;
  std::visit(
      [&out](const auto& data)
      {
        using T = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<T, LedState>)
        {
          out["type"] = "led";
          out["point"] = data.point;
          out["rgb"] = RgbToJson(data.rgb);
          out["pattern"] = PatternName(data.pattern);
          out["v"] = static_cast<std::int64_t>(data.version);
        }
        else if constexpr (std::is_same_v<T, Beep>)
        {
          out["type"] = "beep";
          out["point"] = data.point;
          out["kind"] = BeepKindName(data.kind);
        }
        else if constexpr (std::is_same_v<T, Sound>)
        {
          out["type"] = "sound";
          out["file"] = data.file;
        }
        else if constexpr (std::is_same_v<T, MapLed>)
        {
          out["type"] = "map";
          out["index"] = data.index;
          out["rgb"] = RgbToJson(data.rgb);
          out["pattern"] = PatternName(data.pattern);
        }
        else if constexpr (std::is_same_v<T, PanelPush>)
        {
          // The view's own members sit in the frame, so a panel reads
          // {"type": "panel", "team": "Wolves", "sections": [...]}.
          out["type"] = "panel";
          out["team"] = data.team;
          for (const auto& entry : data.view)
          {
            out[entry.key()] = entry.value();
          }
        }
        else
        {
          static_assert(std::is_same_v<T, Announce>);
          out["type"] = "announce";
          out["text"] = data.text;
          out["to"] = data.to;
        }
      },
      command);
  return out;
}

auto CommandFromJson(const boost::json::object& object) -> Result<Command>
{
  const Result<std::string> type = RequireString(object, "type");
  if (!type)
  {
    return std::unexpected(type.error());
  }

  if (*type == "led")
  {
    const Result<std::string> point = RequireString(object, "point");
    const Result<Rgb> rgb = RgbFromJson(object);
    const Result<Pattern> pattern = PatternFromJson(object);
    const Result<std::int64_t> version = RequireInteger(object, "v");
    if (!point)
    {
      return std::unexpected(point.error());
    }
    if (!rgb)
    {
      return std::unexpected(rgb.error());
    }
    if (!pattern)
    {
      return std::unexpected(pattern.error());
    }
    if (!version)
    {
      return std::unexpected(version.error());
    }
    const Result<std::int64_t> checked =
        RequireInRange("v", *version, 0, UINT16_MAX);
    if (!checked)
    {
      return std::unexpected(checked.error());
    }
    return Command{
        LedState{*point, *rgb, *pattern, static_cast<std::uint16_t>(*checked)}};
  }
  if (*type == "beep")
  {
    const Result<std::string> point = RequireString(object, "point");
    const Result<std::string> kind = RequireString(object, "kind");
    if (!point)
    {
      return std::unexpected(point.error());
    }
    if (!kind)
    {
      return std::unexpected(kind.error());
    }
    const Result<BeepKind> parsed = BeepKindFromName(*kind);
    if (!parsed)
    {
      return std::unexpected(parsed.error());
    }
    return Command{Beep{*point, *parsed}};
  }
  if (*type == "sound")
  {
    const Result<std::string> file = RequireString(object, "file");
    if (!file)
    {
      return std::unexpected(file.error());
    }
    return Command{Sound{*file}};
  }
  if (*type == "map")
  {
    const Result<std::int64_t> index = RequireInteger(object, "index");
    const Result<Rgb> rgb = RgbFromJson(object);
    const Result<Pattern> pattern = PatternFromJson(object);
    if (!index)
    {
      return std::unexpected(index.error());
    }
    if (!rgb)
    {
      return std::unexpected(rgb.error());
    }
    if (!pattern)
    {
      return std::unexpected(pattern.error());
    }
    const Result<std::int64_t> checked =
        RequireInRange("index", *index, 0, INT32_MAX);
    if (!checked)
    {
      return std::unexpected(checked.error());
    }
    return Command{MapLed{static_cast<std::int32_t>(*checked), *rgb, *pattern}};
  }
  if (*type == "panel")
  {
    const Result<std::string> team = RequireString(object, "team");
    if (!team)
    {
      return std::unexpected(team.error());
    }
    boost::json::object view;
    for (const auto& entry : object)
    {
      if (entry.key() != "type" && entry.key() != "team")
      {
        view[entry.key()] = entry.value();
      }
    }
    return Command{PanelPush{*team, std::move(view)}};
  }
  if (*type == "announce")
  {
    const Result<std::string> text = RequireString(object, "text");
    const Result<std::string> to = RequireString(object, "to");
    if (!text)
    {
      return std::unexpected(text.error());
    }
    if (!to)
    {
      return std::unexpected(to.error());
    }
    return Command{Announce{*text, *to}};
  }
  return Fail(ErrorCode::kUnknownKind, "unknown command type: " + *type);
}

}  // namespace sg
