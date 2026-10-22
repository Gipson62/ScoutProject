#ifndef SG_ENGINE_CORE_EVENT_HPP_
#define SG_ENGINE_CORE_EVENT_HPP_

// Everything that happens is an Event; everything the game wants done is a
// Command. Nothing else crosses a module boundary.

#include <boost/json.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

#include "engine/core/error.hpp"

namespace sg
{

// Game time: milliseconds since the session started.
using Millis = std::int64_t;

// Inputs. These are plain aggregates, so their members take no prefix.
struct Tap
{
  std::string point;
  std::string uid;
};

struct Button
{
  std::string point;
};

// From a team panel.
struct Action
{
  std::string team;
  std::string name;
  boost::json::object args;
};

// From the dashboard.
struct Admin
{
  std::string name;
  boost::json::object args;
};

// Undoes one event. Phase 1 logs and replays it; Rebuild honours it in Phase 3.
struct Void
{
  std::uint64_t target = 0;
};

struct Event
{
  // Set by EventLog::Append, never reused.
  std::uint64_t seq = 0;
  // When it really happened, not when its packet arrived.
  Millis t = 0;
  std::variant<Tap, Button, Action, Admin, Void> data;
};

struct Rgb
{
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
};

enum class Pattern : std::uint8_t
{
  kOff,
  kSolid,
  kPulse,
  kBlink,
};

enum class BeepKind : std::uint8_t
{
  kOk = 1,
  kDeny = 2,
  kAlarm = 3,
};

// Outputs. A LedState carries the whole desired state plus a version, never a
// "change colour" delta, so a node that missed one command is corrected by the
// next instead of drifting.
struct LedState
{
  std::string point;
  Rgb rgb;
  Pattern pattern = Pattern::kOff;
  std::uint16_t version = 0;
};

struct Beep
{
  std::string point;
  BeepKind kind = BeepKind::kOk;
};

struct Sound
{
  std::string file;
};

// Any pixel on the HQ map, addressed by index, independent of the points.
struct MapLed
{
  std::int32_t index = 0;
  Rgb rgb;
  Pattern pattern = Pattern::kOff;
};

struct PanelPush
{
  std::string team;
  boost::json::object view;
};

struct Announce
{
  std::string text;
  // A team name, or "all".
  std::string to;
};

using Command =
    std::variant<LedState, Beep, Sound, MapLed, PanelPush, Announce>;

// --- names used on the wire and in the log ---

[[nodiscard]] auto PatternName(Pattern pattern) -> std::string_view;
[[nodiscard]] auto PatternFromName(std::string_view name) -> Result<Pattern>;
[[nodiscard]] auto BeepKindName(BeepKind kind) -> std::string_view;
[[nodiscard]] auto BeepKindFromName(std::string_view name) -> Result<BeepKind>;

// --- JSON, both ways ---
//
// An Event is one object per line of the session log, discriminated by "kind".
// A Command is one WebSocket frame, discriminated by "type", in the shapes
// listed in docs/DESIGN.md. Both directions are needed: the log is written and
// read back, and a frame is produced here and parsed back by the tests and by
// the dashboard's own mirror of the log.

[[nodiscard]] auto ToJson(const Event& event) -> boost::json::object;
[[nodiscard]] auto EventFromJson(const boost::json::object& object)
    -> Result<Event>;

[[nodiscard]] auto ToJson(const Command& command) -> boost::json::object;
[[nodiscard]] auto CommandFromJson(const boost::json::object& object)
    -> Result<Command>;

}  // namespace sg

#endif  // SG_ENGINE_CORE_EVENT_HPP_
