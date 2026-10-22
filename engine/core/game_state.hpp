#ifndef SG_ENGINE_CORE_GAME_STATE_HPP_
#define SG_ENGINE_CORE_GAME_STATE_HPP_

// The whole mutable state of a running game, and the Setup it starts from.
//
// The state is never saved. It is a cache of the event log, rebuilt by
// replaying it, so everything here must be reconstructible from Setup plus the
// log and nothing else — no system clock, no hash order, no leftover from a
// previous run.

#include <boost/json.hpp>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "engine/core/event.hpp"
#include "engine/core/optional_ref.hpp"
#include "engine/core/random.hpp"

namespace sg
{

struct TeamSpec
{
  std::string name;
  Rgb colour;
};

struct PointSpec
{
  std::string id;
  std::string name;
  // Index on the HQ LED map, or -1 for a point that is not on it.
  std::int32_t map_led = -1;
};

// Everything a game starts from: what the config file and the mode name
// provide. M5 fills it from TOML; the tests fill it by hand.
struct Setup
{
  std::string title;
  // The seed of sg::Random. The same seed and the same log give the same game.
  std::uint64_t seed = 0;
  std::vector<TeamSpec> teams;
  std::vector<PointSpec> points;
  // game.p: the live-editable parameters.
  boost::json::object params;
  // The whole config file, read-only for the mode script.
  boost::json::object config;
};

struct TeamState
{
  std::string name;
  Rgb colour;
  // team.res: named counters. Ordered, so a snapshot and its hash do not depend
  // on insertion order.
  std::map<std::string, std::int64_t> resources;
  // team.data: free data owned by the mode script.
  boost::json::object data;
};

struct PointState
{
  std::string id;
  std::string name;
  std::int32_t map_led = -1;
  // The team that holds it, or nothing.
  std::optional<std::string> owner;
  // point.data: free data owned by the mode script.
  boost::json::object data;
  // The last LED state sent for this point, so a resync after a rebuild can
  // restate every point without asking the script.
  LedState led;
};

class GameState
{
 public:
  [[nodiscard]] static auto Initial(const Setup& setup) -> GameState;

  [[nodiscard]] auto Teams() -> std::vector<TeamState>&
  {
    return m_teams;
  }
  [[nodiscard]] auto Teams() const -> const std::vector<TeamState>&
  {
    return m_teams;
  }
  [[nodiscard]] auto Points() -> std::vector<PointState>&
  {
    return m_points;
  }
  [[nodiscard]] auto Points() const -> const std::vector<PointState>&
  {
    return m_points;
  }

  [[nodiscard]] auto FindTeam(std::string_view name) -> OptionalRef<TeamState>;
  [[nodiscard]] auto FindTeam(std::string_view name) const
      -> OptionalRef<const TeamState>;
  [[nodiscard]] auto FindPoint(std::string_view id) -> OptionalRef<PointState>;
  [[nodiscard]] auto FindPoint(std::string_view id) const
      -> OptionalRef<const PointState>;

  // game.data: free global data owned by the mode script.
  [[nodiscard]] auto Data() -> boost::json::object&
  {
    return m_data;
  }
  [[nodiscard]] auto Data() const -> const boost::json::object&
  {
    return m_data;
  }

  // game.p
  [[nodiscard]] auto Params() -> boost::json::object&
  {
    return m_params;
  }
  [[nodiscard]] auto Params() const -> const boost::json::object&
  {
    return m_params;
  }

  // game.config, read-only
  [[nodiscard]] auto Config() const -> const boost::json::object&
  {
    return m_config;
  }

  [[nodiscard]] auto Rng() -> Random&
  {
    return m_rng;
  }

  // A canonical picture of the state: object keys sorted, containers in their
  // own order. Two runs that reached the same state produce the same text.
  [[nodiscard]] auto Snapshot() const -> std::string;

  // A hash of Snapshot(). The replay tests compare these, because comparing two
  // states field by field would miss whatever field the test forgot.
  [[nodiscard]] auto Hash() const -> std::uint64_t;

 private:
  explicit GameState(std::uint64_t seed) : m_rng(seed) {}

  std::vector<TeamState> m_teams;
  std::vector<PointState> m_points;
  boost::json::object m_data;
  boost::json::object m_params;
  boost::json::object m_config;
  Random m_rng;
};

}  // namespace sg

#endif  // SG_ENGINE_CORE_GAME_STATE_HPP_
