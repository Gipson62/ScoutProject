#include "engine/core/game_state.hpp"

#include <algorithm>
#include <boost/json.hpp>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "engine/core/event.hpp"
#include "engine/core/optional_ref.hpp"

namespace sg
{
namespace
{

// boost::json::object keeps insertion order, so serialising it directly would
// make the snapshot depend on the order the script happened to write its keys
// in. This writes objects with their keys sorted instead.
auto WriteCanonical(const boost::json::value& value, std::ostream& out) -> void
{
  switch (value.kind())
  {
    case boost::json::kind::object:
    {
      const boost::json::object& object = value.get_object();
      std::vector<std::string_view> keys;
      keys.reserve(object.size());
      for (const auto& entry : object)
      {
        keys.emplace_back(entry.key());
      }
      std::ranges::sort(keys);

      out << '{';
      bool first = true;
      for (const std::string_view key : keys)
      {
        if (!first)
        {
          out << ',';
        }
        first = false;
        out << boost::json::serialize(
                   boost::json::string_view{key.data(), key.size()})
            << ':';
        WriteCanonical(object.at(key), out);
      }
      out << '}';
      break;
    }
    case boost::json::kind::array:
    {
      out << '[';
      bool first = true;
      for (const boost::json::value& element : value.get_array())
      {
        if (!first)
        {
          out << ',';
        }
        first = false;
        WriteCanonical(element, out);
      }
      out << ']';
      break;
    }
    default:
      out << boost::json::serialize(value);
      break;
  }
}

auto WriteRgb(const Rgb& rgb, std::ostream& out) -> void
{
  out << '[' << static_cast<std::int32_t>(rgb.r) << ','
      << static_cast<std::int32_t>(rgb.g) << ','
      << static_cast<std::int32_t>(rgb.b) << ']';
}

}  // namespace

auto GameState::Initial(const Setup& setup) -> GameState
{
  GameState state{setup.seed};

  state.m_teams.reserve(setup.teams.size());
  for (const TeamSpec& spec : setup.teams)
  {
    state.m_teams.push_back(TeamState{spec.name, spec.colour, {}, {}});
  }

  state.m_points.reserve(setup.points.size());
  for (const PointSpec& spec : setup.points)
  {
    PointState point;
    point.id = spec.id;
    point.name = spec.name;
    point.map_led = spec.map_led;
    point.led = LedState{spec.id, Rgb{}, Pattern::kOff, 0};
    state.m_points.push_back(std::move(point));
  }

  state.m_params = setup.params;
  state.m_config = setup.config;
  return state;
}

auto GameState::FindTeam(std::string_view name) -> OptionalRef<TeamState>
{
  for (TeamState& team : m_teams)
  {
    if (team.name == name)
    {
      return team;
    }
  }
  return std::nullopt;
}

auto GameState::FindTeam(std::string_view name) const
    -> OptionalRef<const TeamState>
{
  for (const TeamState& team : m_teams)
  {
    if (team.name == name)
    {
      return team;
    }
  }
  return std::nullopt;
}

auto GameState::FindPoint(std::string_view id) -> OptionalRef<PointState>
{
  for (PointState& point : m_points)
  {
    if (point.id == id)
    {
      return point;
    }
  }
  return std::nullopt;
}

auto GameState::FindPoint(std::string_view id) const
    -> OptionalRef<const PointState>
{
  for (const PointState& point : m_points)
  {
    if (point.id == id)
    {
      return point;
    }
  }
  return std::nullopt;
}

auto GameState::Snapshot() const -> std::string
{
  std::ostringstream out;

  out << "{\"teams\":[";
  bool first = true;
  for (const TeamState& team : m_teams)
  {
    if (!first)
    {
      out << ',';
    }
    first = false;
    out << "{\"name\":"
        << boost::json::serialize(boost::json::string{team.name})
        << ",\"colour\":";
    WriteRgb(team.colour, out);
    out << ",\"res\":{";
    bool first_resource = true;
    for (const auto& [name, count] : team.resources)
    {
      if (!first_resource)
      {
        out << ',';
      }
      first_resource = false;
      out << boost::json::serialize(boost::json::string{name}) << ':' << count;
    }
    out << "},\"data\":";
    WriteCanonical(team.data, out);
    out << '}';
  }

  out << "],\"points\":[";
  first = true;
  for (const PointState& point : m_points)
  {
    if (!first)
    {
      out << ',';
    }
    first = false;
    out << "{\"id\":" << boost::json::serialize(boost::json::string{point.id})
        << ",\"owner\":";
    if (point.owner.has_value())
    {
      out << boost::json::serialize(boost::json::string{*point.owner});
    }
    else
    {
      out << "null";
    }
    out << ",\"led\":{\"rgb\":";
    WriteRgb(point.led.rgb, out);
    out << ",\"pattern\":"
        << boost::json::serialize(
               boost::json::string{std::string{PatternName(point.led.pattern)}})
        << ",\"v\":" << point.led.version << "},\"data\":";
    WriteCanonical(point.data, out);
    out << '}';
  }

  out << "],\"data\":";
  WriteCanonical(m_data, out);
  out << ",\"params\":";
  WriteCanonical(m_params, out);

  // The generator's own position is part of the state: two runs that agree on
  // everything else but have drawn a different number of values would diverge
  // on the next roll.
  out << ",\"rng\":" << m_rng.State() << '}';

  return out.str();
}

auto GameState::Hash() const -> std::uint64_t
{
  // FNV-1a, 64-bit: short, stable, and specified down to the constants, so a
  // hash printed in a bug report from camp can be reproduced here.
  constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
  constexpr std::uint64_t kPrime = 1099511628211ULL;

  const std::string snapshot = Snapshot();
  std::uint64_t hash = kOffsetBasis;
  for (const char byte : snapshot)
  {
    hash ^= static_cast<std::uint8_t>(byte);
    hash *= kPrime;
  }
  return hash;
}

}  // namespace sg
