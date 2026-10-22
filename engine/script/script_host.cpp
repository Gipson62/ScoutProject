#include "engine/script/script_host.hpp"

#include <boost/json.hpp>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "engine/config/config.hpp"
#include "engine/core/error.hpp"
#include "engine/core/event.hpp"
#include "engine/core/game_state.hpp"
#include "engine/core/mode.hpp"
#include "engine/core/optional_ref.hpp"
#include "engine/core/panic.hpp"
#include "engine/core/scheduler.hpp"

// pocketpy is a C library, and this is the one translation unit that talks to
// it. py_Ref and friends are raw pointers into the interpreter's own storage;
// none of them is stored anywhere outside this file.
extern "C"
{
#include <pocketpy.h>
}

namespace sg
{
namespace
{

// The Python half of the module. Decorators, the hook lists and the dispatchers
// live here rather than in C, because this is where they are short and
// obviously right; the C side only provides what needs the engine.
constexpr const char* kPrelude = R"PY(
_on_start = []
_on_tag = []
_on_button = []
_timers = {}

class ActionError(Exception):
    pass

class _Namespace:
    pass

def on_start(fn):
    _on_start.append(fn)
    return fn

def on_tag(point=None, team=None, role=None):
    def deco(fn):
        _on_tag.append((fn, point, team, role))
        return fn
    return deco

def on_button(point=None):
    def deco(fn):
        _on_button.append((fn, point))
        return fn
    return deco

def minutes(n):
    return int(n * 60000)

def seconds(n):
    return int(n * 1000)

def _dispatch_start(game):
    for fn in _on_start:
        fn(game)

def _dispatch_tag(game, point, tag):
    for entry in _on_tag:
        fn = entry[0]
        if entry[1] is not None and entry[1] != point.id:
            continue
        if entry[2] is not None and entry[2] != tag.team:
            continue
        if entry[3] is not None and entry[3] != tag.role:
            continue
        fn(game, point, tag)

def _dispatch_button(game, point):
    for entry in _on_button:
        if entry[1] is not None and entry[1] != point.id:
            continue
        entry[0](game, point)

def _remember_timer(tid, fn, args, repeat):
    _timers[tid] = (fn, args, repeat)

def _forget_timer(tid):
    if tid in _timers:
        del _timers[tid]

def _dispatch_timer(game, tid):
    if tid not in _timers:
        return
    entry = _timers[tid]
    fn = entry[0]
    args = entry[1]
    if not entry[2]:
        del _timers[tid]
    # Spelled out rather than fn(game, *args): three arguments covers every
    # callback a mode needs, and this works whatever the interpreter does with
    # star-arguments.
    if len(args) == 0:
        fn(game)
    elif len(args) == 1:
        fn(game, args[0])
    elif len(args) == 2:
        fn(game, args[0], args[1])
    else:
        fn(game, args[0], args[1], args[2])
)PY";

constexpr std::int32_t kGameSlotTeams = 0;
constexpr std::int32_t kGameSlotPoints = 1;
constexpr std::int32_t kGameSlotParams = 2;
constexpr std::int32_t kGameSlotData = 3;
constexpr std::int32_t kGameSlotConfig = 4;
constexpr std::int32_t kGameSlotRandom = 5;
constexpr std::int32_t kGameSlotCount = 6;

constexpr std::int32_t kPointSlotData = 0;
constexpr std::int32_t kPointSlotCount = 1;

constexpr std::int32_t kTeamSlotData = 0;
constexpr std::int32_t kTeamSlotRes = 1;
constexpr std::int32_t kTeamSlotCount = 2;

constexpr std::int32_t kTagSlotUid = 0;
constexpr std::int32_t kTagSlotTeam = 1;
constexpr std::int32_t kTagSlotPlayer = 2;
constexpr std::int32_t kTagSlotRole = 3;
constexpr std::int32_t kTagSlotCount = 4;

[[nodiscard]] auto HexOf(const Rgb& rgb) -> std::string
{
  constexpr const char* kDigits = "0123456789abcdef";
  std::string out = "#";
  for (const std::uint8_t channel : {rgb.r, rgb.g, rgb.b})
  {
    out += kDigits[channel >> 4U];
    out += kDigits[channel & 0x0FU];
  }
  return out;
}

}  // namespace

struct ScriptHost::Impl
{
  // Set for the duration of one hook. OptionalRef, not a pointer, because the
  // guidelines allow exactly one raw pointer in the repository and it is not
  // this one.
  OptionalRef<GameState> state;
  OptionalRef<ModeHost> host;

  py_GlobalRef module = nullptr;
  py_Type game_type = 0;
  py_Type point_type = 0;
  py_Type team_type = 0;
  py_Type tag_type = 0;
  py_Type random_type = 0;
  bool objects_built = false;

  std::optional<std::string> load_error;
  std::vector<std::string> errors;

  // --- plumbing ---

  [[nodiscard]] auto TakeError(std::string_view what) -> bool
  {
    if (!py_checkexc())
    {
      return false;
    }
    std::string message{what};
    char* formatted = py_formatexc();
    if (formatted != nullptr)
    {
      message += ": ";
      message += formatted;
      py_free(formatted);
    }
    errors.push_back(std::move(message));
    py_clearexc(nullptr);
    return true;
  }

  [[nodiscard]] auto ModuleItem(const char* name) -> py_ItemRef
  {
    py_ItemRef item = py_getdict(module, py_name(name));
    SG_ASSERT(item != nullptr,
              "the scoutgame module lost one of its own names");
    return item;
  }

  [[nodiscard]] auto Game() -> py_ItemRef
  {
    return ModuleItem("_game");
  }

  // Converts a JSON value into a Python one. The engine's own data — params and
  // the config file — comes in this way.
  auto PushJson(py_OutRef out, const boost::json::value& value) -> void
  {
    const std::string text = boost::json::serialize(value);
    if (!py_json_loads(text.c_str()))
    {
      static_cast<void>(TakeError("cannot convert config to Python"));
      py_newnone(out);
      return;
    }
    py_assign(out, py_retval());
  }

  // The other direction: what a script left in a dict, as JSON, so the state
  // snapshot and its hash can see it. A Team or a Point stored in script data —
  // which the design's own domination mode does — becomes a tagged string: the
  // mirror only has to tell two states apart, never to be read back.
  [[nodiscard]] auto ToJson(py_Ref value) -> boost::json::value;

  struct DictToJson
  {
    Impl* impl = nullptr;
    boost::json::object out;
  };

  static auto DictEntryToJson(py_Ref key, py_Ref value, void* ctx) -> bool
  {
    auto* collector = static_cast<DictToJson*>(ctx);
    const std::string name = py_isstr(key) ? py_tostr(key) : std::string{"?"};
    collector->out[name] = collector->impl->ToJson(value);
    return true;
  }

  // --- object construction ---

  auto BuildObjects() -> void;
  auto SyncToState() -> void;

  [[nodiscard]] auto CallDispatch(const char* name, std::span<py_Ref> args)
      -> bool
  {
    py_ItemRef fn = py_getdict(module, py_name(name));
    if (fn == nullptr)
    {
      errors.emplace_back(std::string{"the scoutgame module has no "} + name);
      return false;
    }
    py_push(fn);
    py_pushnil();
    for (py_Ref arg : args)
    {
      py_push(arg);
    }
    if (!py_vectorcall(static_cast<std::uint16_t>(args.size()), 0))
    {
      static_cast<void>(TakeError(name));
      return false;
    }
    return true;
  }
};

namespace
{

[[nodiscard]] auto Self() -> ScriptHost::Impl&
{
  void* ctx = py_getvmctx();
  SG_ASSERT(ctx != nullptr,
            "a scoutgame binding ran with no ScriptHost behind it");
  return *static_cast<ScriptHost::Impl*>(ctx);
}

[[nodiscard]] auto IndexOf(py_Ref self) -> std::size_t
{
  return static_cast<std::size_t>(
      *static_cast<const std::int32_t*>(py_touserdata(self)));
}

[[nodiscard]] auto PointOf(py_Ref self) -> PointState&
{
  ScriptHost::Impl& impl = Self();
  SG_ASSERT(impl.state.has_value(), "a point was used outside a hook");
  return impl.state->Points()[IndexOf(self)];
}

[[nodiscard]] auto TeamOf(py_Ref self) -> TeamState&
{
  ScriptHost::Impl& impl = Self();
  SG_ASSERT(impl.state.has_value(), "a team was used outside a hook");
  return impl.state->Teams()[IndexOf(self)];
}

// --- game ---

auto GameNow(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  static_cast<void>(argv);
  py_newint(py_retval(), Self().host->Now());
  return true;
}

auto GameSlot(std::int32_t slot) -> py_CFunction;

auto GameTeam(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  PY_CHECK_ARG_TYPE(1, tp_str);
  py_Ref teams = py_getslot(&argv[0], kGameSlotTeams);
  const char* wanted = py_tostr(&argv[1]);
  for (int i = 0; i < py_list_len(teams); ++i)
  {
    py_ItemRef team = py_list_getitem(teams, i);
    if (TeamOf(team).name == wanted)
    {
      py_assign(py_retval(), team);
      return true;
    }
  }
  py_newnone(py_retval());
  return true;
}

auto GamePoint(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  PY_CHECK_ARG_TYPE(1, tp_str);
  py_Ref points = py_getslot(&argv[0], kGameSlotPoints);
  const char* wanted = py_tostr(&argv[1]);
  for (int i = 0; i < py_list_len(points); ++i)
  {
    py_ItemRef point = py_list_getitem(points, i);
    if (PointOf(point).id == wanted)
    {
      py_assign(py_retval(), point);
      return true;
    }
  }
  py_newnone(py_retval());
  return true;
}

// game.after(delay, fn, *args) and game.every(interval, fn, *args). The handle
// is remembered on the Python side, keyed by the engine's own timer id, so the
// id a script stores in point.data is the id the scheduler hands out and a
// replay reproduces.
[[nodiscard]] auto Schedule(int argc, py_StackRef argv, bool repeat) -> bool
{
  ScriptHost::Impl& impl = Self();
  // The signature is "after(self, delay, fn, *args)", so the interpreter has
  // already collected the extra arguments into one tuple at argv[3].
  if (argc != 4)
  {
    return TypeError("after() needs a delay and a function");
  }
  py_i64 delay = 0;
  if (!py_castint(&argv[1], &delay))
  {
    return false;
  }
  if (!py_callable(&argv[2]))
  {
    return TypeError("after() needs a function to call");
  }
  if (repeat && delay <= 0)
  {
    return ValueError("every() needs a positive interval");
  }

  const TimerId id = repeat ? impl.host->Every(delay) : impl.host->After(delay);

  // _remember_timer(id, fn, args, repeat): the callable and its arguments live
  // on the Python side, keyed by the engine's own timer id, so the id a script
  // stores in point.data is the id the scheduler handed out and a replay
  // reproduces it.
  py_push(impl.ModuleItem("_remember_timer"));
  py_pushnil();
  py_newint(py_pushtmp(), static_cast<py_i64>(id));
  py_push(&argv[2]);
  py_push(&argv[3]);
  py_newbool(py_pushtmp(), repeat);
  if (!py_vectorcall(4, 0))
  {
    return false;
  }

  py_newint(py_retval(), static_cast<py_i64>(id));
  return true;
}

auto GameAfter(int argc, py_StackRef argv) -> bool
{
  return Schedule(argc, argv, false);
}

auto GameEvery(int argc, py_StackRef argv) -> bool
{
  return Schedule(argc, argv, true);
}

auto GameCancel(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  ScriptHost::Impl& impl = Self();
  if (py_isnone(&argv[1]))
  {
    // Cancelling "no timer" is normal: a script keeps None in point.data until
    // a countdown is running.
    py_newbool(py_retval(), false);
    return true;
  }
  py_i64 id = 0;
  if (!py_castint(&argv[1], &id))
  {
    return false;
  }
  const bool cancelled = impl.host->Cancel(static_cast<TimerId>(id));

  py_Ref forget = impl.ModuleItem("_forget_timer");
  py_push(forget);
  py_pushnil();
  py_newint(py_pushtmp(), id);
  if (!py_vectorcall(1, 0))
  {
    return false;
  }

  py_newbool(py_retval(), cancelled);
  return true;
}

auto GameAnnounce(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  PY_CHECK_ARG_TYPE(1, tp_str);
  std::string to = "all";
  if (py_isstr(&argv[2]))
  {
    to = py_tostr(&argv[2]);
  }
  else if (py_istype(&argv[2], Self().team_type))
  {
    to = TeamOf(&argv[2]).name;
  }
  Self().host->Emit(Command{Announce{py_tostr(&argv[1]), std::move(to)}});
  py_newnone(py_retval());
  return true;
}

auto GameSound(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  PY_CHECK_ARG_TYPE(1, tp_str);
  Self().host->Emit(Command{Sound{py_tostr(&argv[1])}});
  py_newnone(py_retval());
  return true;
}

auto GameMap(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  py_i64 index = 0;
  if (!py_castint(&argv[1], &index))
  {
    return false;
  }
  PY_CHECK_ARG_TYPE(2, tp_str);
  const Result<Rgb> colour = ParseColour(py_tostr(&argv[2]));
  if (!colour)
  {
    return ValueError("%s", colour.error().message.c_str());
  }
  const char* pattern_name = py_isstr(&argv[3]) ? py_tostr(&argv[3]) : "solid";
  const Result<Pattern> pattern = PatternFromName(pattern_name);
  if (!pattern)
  {
    return ValueError("%s", pattern.error().message.c_str());
  }
  Self().host->Emit(
      Command{MapLed{static_cast<std::int32_t>(index), *colour, *pattern}});
  py_newnone(py_retval());
  return true;
}

// --- point ---

auto PointId(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  py_newstr(py_retval(), PointOf(&argv[0]).id.c_str());
  return true;
}

auto PointName(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  py_newstr(py_retval(), PointOf(&argv[0]).name.c_str());
  return true;
}

auto PointData(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  py_assign(py_retval(), py_getslot(&argv[0], kPointSlotData));
  return true;
}

// point.owner is the Team object, as the design's scripts expect, while the
// state stores only its name.
auto PointGetOwner(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  const PointState& point = PointOf(&argv[0]);
  if (!point.owner.has_value())
  {
    py_newnone(py_retval());
    return true;
  }
  py_Ref teams = py_getslot(Self().Game(), kGameSlotTeams);
  for (int i = 0; i < py_list_len(teams); ++i)
  {
    py_ItemRef team = py_list_getitem(teams, i);
    if (TeamOf(team).name == *point.owner)
    {
      py_assign(py_retval(), team);
      return true;
    }
  }
  py_newnone(py_retval());
  return true;
}

auto PointSetOwner(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  PointState& point = PointOf(&argv[0]);
  if (py_isnone(&argv[1]))
  {
    point.owner = std::nullopt;
  }
  else if (py_istype(&argv[1], Self().team_type))
  {
    point.owner = TeamOf(&argv[1]).name;
  }
  else if (py_isstr(&argv[1]))
  {
    point.owner = py_tostr(&argv[1]);
  }
  else
  {
    return TypeError("point.owner takes a team, a team name or None");
  }
  py_newnone(py_retval());
  return true;
}

auto PointLight(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  PY_CHECK_ARG_TYPE(1, tp_str);
  const Result<Rgb> colour = ParseColour(py_tostr(&argv[1]));
  if (!colour)
  {
    return ValueError("%s", colour.error().message.c_str());
  }
  const char* pattern_name = py_isstr(&argv[2]) ? py_tostr(&argv[2]) : "solid";
  const Result<Pattern> pattern = PatternFromName(pattern_name);
  if (!pattern)
  {
    return ValueError("%s", pattern.error().message.c_str());
  }
  // The version is left at 0: the Core assigns it when the command is routed.
  Self().host->Emit(
      Command{LedState{PointOf(&argv[0]).id, *colour, *pattern, 0}});
  py_newnone(py_retval());
  return true;
}

auto PointBeep(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  const char* kind_name = py_isstr(&argv[1]) ? py_tostr(&argv[1]) : "ok";
  const Result<BeepKind> kind = BeepKindFromName(kind_name);
  if (!kind)
  {
    return ValueError("%s", kind.error().message.c_str());
  }
  Self().host->Emit(Command{Beep{PointOf(&argv[0]).id, *kind}});
  py_newnone(py_retval());
  return true;
}

// --- team ---

auto TeamName(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  py_newstr(py_retval(), TeamOf(&argv[0]).name.c_str());
  return true;
}

auto TeamColour(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  py_newstr(py_retval(), HexOf(TeamOf(&argv[0]).colour).c_str());
  return true;
}

auto TeamRes(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  py_assign(py_retval(), py_getslot(&argv[0], kTeamSlotRes));
  return true;
}

auto TeamData(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  py_assign(py_retval(), py_getslot(&argv[0], kTeamSlotData));
  return true;
}

// --- tag ---

auto TagSlotGetter(std::int32_t slot, py_StackRef argv) -> bool
{
  py_assign(py_retval(), py_getslot(&argv[0], slot));
  return true;
}

auto TagUid(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  return TagSlotGetter(kTagSlotUid, argv);
}

auto TagTeam(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  // tag.team is the Team object when the wristband is enrolled, so a script can
  // write point.owner = tag.team; an unknown band gives None.
  const char* name = py_tostr(py_getslot(&argv[0], kTagSlotTeam));
  if (*name == '\0')
  {
    py_newnone(py_retval());
    return true;
  }
  py_Ref teams = py_getslot(Self().Game(), kGameSlotTeams);
  for (int i = 0; i < py_list_len(teams); ++i)
  {
    py_ItemRef team = py_list_getitem(teams, i);
    if (TeamOf(team).name == name)
    {
      py_assign(py_retval(), team);
      return true;
    }
  }
  py_newnone(py_retval());
  return true;
}

auto TagPlayer(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  return TagSlotGetter(kTagSlotPlayer, argv);
}

auto TagRole(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  return TagSlotGetter(kTagSlotRole, argv);
}

// --- random ---

auto RandomRandom(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  static_cast<void>(argv);
  py_newfloat(py_retval(), Self().state->Rng().NextDouble());
  return true;
}

auto RandomUniform(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  py_f64 low = 0;
  py_f64 high = 0;
  if (!py_castfloat(&argv[1], &low) || !py_castfloat(&argv[2], &high))
  {
    return false;
  }
  py_newfloat(py_retval(), Self().state->Rng().NextUniform(low, high));
  return true;
}

auto RandomRandint(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  py_i64 low = 0;
  py_i64 high = 0;
  if (!py_castint(&argv[1], &low) || !py_castint(&argv[2], &high))
  {
    return false;
  }
  if (low > high)
  {
    return ValueError("randint() needs low <= high");
  }
  py_newint(py_retval(), Self().state->Rng().NextInRange(low, high));
  return true;
}

// The game.<slot> properties are all the same function with a different slot,
// which a C function cannot capture, so there is one tiny function each.
auto GameTeams(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  py_assign(py_retval(), py_getslot(&argv[0], kGameSlotTeams));
  return true;
}

auto GamePoints(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  py_assign(py_retval(), py_getslot(&argv[0], kGameSlotPoints));
  return true;
}

auto GameParams(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  py_assign(py_retval(), py_getslot(&argv[0], kGameSlotParams));
  return true;
}

auto GameData(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  py_assign(py_retval(), py_getslot(&argv[0], kGameSlotData));
  return true;
}

auto GameConfig(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  py_assign(py_retval(), py_getslot(&argv[0], kGameSlotConfig));
  return true;
}

auto GameRandom(int argc, py_StackRef argv) -> bool
{
  static_cast<void>(argc);
  py_assign(py_retval(), py_getslot(&argv[0], kGameSlotRandom));
  return true;
}

auto GameSlot(std::int32_t slot) -> py_CFunction
{
  switch (slot)
  {
    case kGameSlotTeams:
      return GameTeams;
    case kGameSlotPoints:
      return GamePoints;
    case kGameSlotParams:
      return GameParams;
    case kGameSlotData:
      return GameData;
    case kGameSlotConfig:
      return GameConfig;
    default:
      return GameRandom;
  }
}

}  // namespace

auto ScriptHost::Impl::ToJson(py_Ref value) -> boost::json::value
{
  if (py_isnone(value) || py_isnil(value))
  {
    return boost::json::value{};
  }
  if (py_isbool(value))
  {
    return py_tobool(value);
  }
  if (py_isint(value))
  {
    return py_toint(value);
  }
  if (py_isfloat(value))
  {
    return py_tofloat(value);
  }
  if (py_isstr(value))
  {
    return boost::json::string{py_tostr(value)};
  }
  if (py_istype(value, team_type))
  {
    // A mode may keep a Team in its data; the design's domination mode does.
    return boost::json::string{"team:" + state->Teams()[IndexOf(value)].name};
  }
  if (py_istype(value, point_type))
  {
    return boost::json::string{"point:" + state->Points()[IndexOf(value)].id};
  }
  if (py_islist(value) || py_istuple(value))
  {
    boost::json::array out;
    const int size =
        py_islist(value) ? py_list_len(value) : py_tuple_len(value);
    for (int i = 0; i < size; ++i)
    {
      out.push_back(ToJson(py_islist(value) ? py_list_getitem(value, i)
                                            : py_tuple_getitem(value, i)));
    }
    return out;
  }
  if (py_isdict(value))
  {
    DictToJson collector{this, {}};
    if (!py_dict_apply(value, &Impl::DictEntryToJson, &collector))
    {
      static_cast<void>(TakeError("reading script data"));
    }
    return collector.out;
  }
  // Anything else is named by its type, which is enough to tell two states
  // apart without pretending to serialise it.
  return boost::json::string{std::string{"<"} + py_tpname(py_typeof(value)) +
                             ">"};
}

auto ScriptHost::Impl::BuildObjects() -> void
{
  if (objects_built)
  {
    return;
  }
  SG_ASSERT(state.has_value(), "BuildObjects ran with no state");

  py_ItemRef game = py_emplacedict(module, py_name("_game"));
  static_cast<void>(py_newobject(game, game_type, kGameSlotCount, 0));

  py_newlist(py_getslot(game, kGameSlotTeams));
  for (std::size_t i = 0; i < state->Teams().size(); ++i)
  {
    py_Ref slot = py_list_emplace(py_getslot(game, kGameSlotTeams));
    void* ud =
        py_newobject(slot, team_type, kTeamSlotCount, sizeof(std::int32_t));
    *static_cast<std::int32_t*>(ud) = static_cast<std::int32_t>(i);
    py_newdict(py_getslot(slot, kTeamSlotData));
    py_newdict(py_getslot(slot, kTeamSlotRes));
  }

  py_newlist(py_getslot(game, kGameSlotPoints));
  for (std::size_t i = 0; i < state->Points().size(); ++i)
  {
    py_Ref slot = py_list_emplace(py_getslot(game, kGameSlotPoints));
    void* ud =
        py_newobject(slot, point_type, kPointSlotCount, sizeof(std::int32_t));
    *static_cast<std::int32_t*>(ud) = static_cast<std::int32_t>(i);
    py_newdict(py_getslot(slot, kPointSlotData));
  }

  PushJson(py_getslot(game, kGameSlotConfig), state->Config());
  static_cast<void>(
      py_newobject(py_getslot(game, kGameSlotRandom), random_type, 0, 0));

  // game.p is an object, not a dict, because a script reads
  // game.p.turn_minutes.
  py_Ref namespace_type = ModuleItem("_Namespace");
  if (!py_call(namespace_type, 0, nullptr))
  {
    static_cast<void>(TakeError("creating game.p"));
    py_newnone(py_getslot(game, kGameSlotParams));
  }
  else
  {
    py_assign(py_getslot(game, kGameSlotParams), py_retval());
    for (const auto& entry : state->Params())
    {
      py_Ref value = py_emplacedict(
          py_getslot(game, kGameSlotParams),
          py_namev(c11_sv{entry.key().data(),
                          static_cast<int>(entry.key().size())}));
      PushJson(value, entry.value());
    }
  }

  py_newdict(py_getslot(game, kGameSlotData));
  objects_built = true;
}

auto ScriptHost::Impl::SyncToState() -> void
{
  SG_ASSERT(state.has_value(), "SyncToState ran with no state");
  py_Ref game = Game();

  const boost::json::value data = ToJson(py_getslot(game, kGameSlotData));
  state->Data() = data.is_object() ? data.get_object() : boost::json::object{};

  py_Ref teams = py_getslot(game, kGameSlotTeams);
  for (int i = 0; i < py_list_len(teams); ++i)
  {
    py_ItemRef team = py_list_getitem(teams, i);
    TeamState& out = state->Teams()[static_cast<std::size_t>(i)];

    const boost::json::value team_data =
        ToJson(py_getslot(team, kTeamSlotData));
    out.data =
        team_data.is_object() ? team_data.get_object() : boost::json::object{};

    // Counters are integers: a resource is a number of things.
    out.resources.clear();
    const boost::json::value res = ToJson(py_getslot(team, kTeamSlotRes));
    if (const boost::json::object* object = res.if_object())
    {
      for (const auto& entry : *object)
      {
        boost::system::error_code ec;
        const std::int64_t count = entry.value().to_number<std::int64_t>(ec);
        if (!ec)
        {
          out.resources[std::string{entry.key()}] = count;
        }
      }
    }
  }

  py_Ref points = py_getslot(game, kGameSlotPoints);
  for (int i = 0; i < py_list_len(points); ++i)
  {
    py_ItemRef point = py_list_getitem(points, i);
    const boost::json::value point_data =
        ToJson(py_getslot(point, kPointSlotData));
    state->Points()[static_cast<std::size_t>(i)].data =
        point_data.is_object() ? point_data.get_object()
                               : boost::json::object{};
  }
}

namespace
{

// pocketpy's interpreter is process-wide and cannot be initialised twice: after
// py_finalize it refuses to come back. So it is started once, on first use, and
// wiped with py_resetvm for every host after that — which is exactly what a
// rebuild wants, a VM with nothing left in it. The function-local static shuts
// it down at process exit.
class Interpreter
{
 public:
  static auto Fresh() -> void
  {
    static Interpreter instance;
    if (instance.m_used)
    {
      py_resetvm();
    }
    instance.m_used = true;
  }

  Interpreter(const Interpreter&) = delete;
  Interpreter(Interpreter&&) = delete;
  auto operator=(const Interpreter&) -> Interpreter& = delete;
  auto operator=(Interpreter&&) -> Interpreter& = delete;

 private:
  Interpreter()
  {
    py_initialize();
  }
  ~Interpreter()
  {
    py_finalize();
  }

  bool m_used = false;
};

// Two hosts cannot share one VM. Rebuild destroys the old host before asking
// for a new one, which is what keeps this at one.
std::int32_t g_live_hosts = 0;

}  // namespace

ScriptHost::ScriptHost(std::string_view source, std::string_view name)
    : m_impl(std::make_unique<Impl>())
{
  SG_ASSERT(g_live_hosts == 0,
            "a second ScriptHost was created while one was alive");
  ++g_live_hosts;

  Interpreter::Fresh();
  py_setvmctx(m_impl.get());

  m_impl->module = py_newmodule("scoutgame");

  m_impl->game_type = py_newtype("Game", tp_object, m_impl->module, nullptr);
  m_impl->point_type = py_newtype("Point", tp_object, m_impl->module, nullptr);
  m_impl->team_type = py_newtype("Team", tp_object, m_impl->module, nullptr);
  m_impl->tag_type = py_newtype("Tag", tp_object, m_impl->module, nullptr);
  m_impl->random_type =
      py_newtype("Random", tp_object, m_impl->module, nullptr);

  py_bindproperty(m_impl->game_type, "now", GameNow, nullptr);
  py_bindproperty(m_impl->game_type, "teams", GameSlot(kGameSlotTeams),
                  nullptr);
  py_bindproperty(m_impl->game_type, "points", GameSlot(kGameSlotPoints),
                  nullptr);
  py_bindproperty(m_impl->game_type, "p", GameSlot(kGameSlotParams), nullptr);
  py_bindproperty(m_impl->game_type, "data", GameSlot(kGameSlotData), nullptr);
  py_bindproperty(m_impl->game_type, "config", GameSlot(kGameSlotConfig),
                  nullptr);
  py_bindproperty(m_impl->game_type, "random", GameSlot(kGameSlotRandom),
                  nullptr);
  py_bind(py_tpobject(m_impl->game_type), "after(self, delay, fn, *args)",
          GameAfter);
  py_bind(py_tpobject(m_impl->game_type), "every(self, interval, fn, *args)",
          GameEvery);
  py_bind(py_tpobject(m_impl->game_type), "cancel(self, timer)", GameCancel);
  py_bind(py_tpobject(m_impl->game_type), "announce(self, text, to='all')",
          GameAnnounce);
  py_bind(py_tpobject(m_impl->game_type), "sound(self, file)", GameSound);
  py_bind(py_tpobject(m_impl->game_type),
          "map(self, index, colour, pattern='solid')", GameMap);
  py_bind(py_tpobject(m_impl->game_type), "team(self, name)", GameTeam);
  py_bind(py_tpobject(m_impl->game_type), "point(self, id)", GamePoint);

  py_bindproperty(m_impl->point_type, "id", PointId, nullptr);
  py_bindproperty(m_impl->point_type, "name", PointName, nullptr);
  py_bindproperty(m_impl->point_type, "data", PointData, nullptr);
  py_bindproperty(m_impl->point_type, "owner", PointGetOwner, PointSetOwner);
  py_bind(py_tpobject(m_impl->point_type),
          "light(self, colour, pattern='solid')", PointLight);
  py_bind(py_tpobject(m_impl->point_type), "beep(self, kind='ok')", PointBeep);

  py_bindproperty(m_impl->team_type, "name", TeamName, nullptr);
  py_bindproperty(m_impl->team_type, "colour", TeamColour, nullptr);
  py_bindproperty(m_impl->team_type, "res", TeamRes, nullptr);
  py_bindproperty(m_impl->team_type, "data", TeamData, nullptr);

  py_bindproperty(m_impl->tag_type, "uid", TagUid, nullptr);
  py_bindproperty(m_impl->tag_type, "team", TagTeam, nullptr);
  py_bindproperty(m_impl->tag_type, "player", TagPlayer, nullptr);
  py_bindproperty(m_impl->tag_type, "role", TagRole, nullptr);

  py_bind(py_tpobject(m_impl->random_type), "random(self)", RandomRandom);
  py_bind(py_tpobject(m_impl->random_type), "uniform(self, low, high)",
          RandomUniform);
  py_bind(py_tpobject(m_impl->random_type), "randint(self, low, high)",
          RandomRandint);

  if (!py_exec(kPrelude, "<scoutgame>", EXEC_MODE, m_impl->module))
  {
    static_cast<void>(m_impl->TakeError("the scoutgame prelude"));
    m_impl->load_error = m_impl->errors.back();
    return;
  }

  // The mode script runs in its own module, so its helpers do not land in
  // scoutgame, and `from scoutgame import ...` finds the real module.
  py_GlobalRef mode_module = py_newmodule(std::string{name}.c_str());
  if (!py_exec(std::string{source}.c_str(), std::string{name}.c_str(),
               EXEC_MODE, mode_module))
  {
    static_cast<void>(
        m_impl->TakeError(std::string{"loading "} + std::string{name}));
    m_impl->load_error = m_impl->errors.back();
  }
}

ScriptHost::~ScriptHost()
{
  // The VM itself outlives us; the next host wipes it. Clearing the context
  // makes a stray callback panic instead of reaching a destroyed host.
  py_setvmctx(nullptr);
  --g_live_hosts;
}

auto ScriptHost::LoadError() const -> std::optional<std::string>
{
  return m_impl->load_error;
}

auto ScriptHost::Errors() const -> std::span<const std::string>
{
  return m_impl->errors;
}

auto ScriptHost::OnStart(GameState& state, ModeHost& host) -> void
{
  if (m_impl->load_error.has_value())
  {
    return;
  }
  m_impl->state = state;
  m_impl->host = host;
  m_impl->BuildObjects();

  py_Ref game = m_impl->Game();
  std::array<py_Ref, 1> args{game};
  if (m_impl->CallDispatch("_dispatch_start", args))
  {
    m_impl->SyncToState();
  }
  m_impl->state = std::nullopt;
  m_impl->host = std::nullopt;
}

auto ScriptHost::OnTag(GameState& state, ModeHost& host, std::string_view point,
                       const Tag& tag) -> void
{
  if (m_impl->load_error.has_value())
  {
    return;
  }
  m_impl->state = state;
  m_impl->host = host;

  py_Ref game = m_impl->Game();
  py_Ref points = py_getslot(game, kGameSlotPoints);
  py_ItemRef found = nullptr;
  for (int i = 0; i < py_list_len(points); ++i)
  {
    py_ItemRef candidate = py_list_getitem(points, i);
    if (state.Points()[static_cast<std::size_t>(i)].id == point)
    {
      found = candidate;
      break;
    }
  }
  if (found == nullptr)
  {
    // A tap on a point the config does not know: the log keeps it, the rules
    // cannot act on it.
    m_impl->errors.push_back("tap on unknown point " + std::string{point});
    m_impl->state = std::nullopt;
    m_impl->host = std::nullopt;
    return;
  }

  py_Ref tag_object = py_pushtmp();
  static_cast<void>(
      py_newobject(tag_object, m_impl->tag_type, kTagSlotCount, 0));
  py_newstr(py_getslot(tag_object, kTagSlotUid), tag.uid.c_str());
  py_newstr(py_getslot(tag_object, kTagSlotTeam), tag.team.c_str());
  py_newstr(py_getslot(tag_object, kTagSlotPlayer), tag.player.c_str());
  py_newstr(py_getslot(tag_object, kTagSlotRole), tag.role.c_str());

  std::array<py_Ref, 3> args{game, found, tag_object};
  if (m_impl->CallDispatch("_dispatch_tag", args))
  {
    m_impl->SyncToState();
  }
  py_pop();

  m_impl->state = std::nullopt;
  m_impl->host = std::nullopt;
}

auto ScriptHost::OnButton(GameState& state, ModeHost& host,
                          std::string_view point) -> void
{
  if (m_impl->load_error.has_value())
  {
    return;
  }
  m_impl->state = state;
  m_impl->host = host;

  py_Ref game = m_impl->Game();
  py_Ref points = py_getslot(game, kGameSlotPoints);
  for (int i = 0; i < py_list_len(points); ++i)
  {
    if (state.Points()[static_cast<std::size_t>(i)].id != point)
    {
      continue;
    }
    std::array<py_Ref, 2> args{game, py_list_getitem(points, i)};
    if (m_impl->CallDispatch("_dispatch_button", args))
    {
      m_impl->SyncToState();
    }
    break;
  }

  m_impl->state = std::nullopt;
  m_impl->host = std::nullopt;
}

auto ScriptHost::OnTimer(GameState& state, ModeHost& host, TimerId id) -> void
{
  if (m_impl->load_error.has_value())
  {
    return;
  }
  m_impl->state = state;
  m_impl->host = host;

  py_Ref game = m_impl->Game();
  py_Ref timer_id = py_pushtmp();
  py_newint(timer_id, static_cast<py_i64>(id));
  std::array<py_Ref, 2> args{game, timer_id};
  if (m_impl->CallDispatch("_dispatch_timer", args))
  {
    m_impl->SyncToState();
  }
  py_pop();

  m_impl->state = std::nullopt;
  m_impl->host = std::nullopt;
}

}  // namespace sg
