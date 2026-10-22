#ifndef SG_ENGINE_CONFIG_ROSTER_HPP_
#define SG_ENGINE_CONFIG_ROSTER_HPP_

// The roster: which wristband belongs to which player, team and role. A chief
// fills it in a spreadsheet and exports CSV, so the reader accepts what a
// spreadsheet produces and names the file and the line when it cannot.

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

#include "engine/core/error.hpp"
#include "engine/core/mode.hpp"

namespace sg
{

class CsvRoster final : public Roster
{
 public:
  // The file starts with a header row naming its columns. `uid` (or `tag`, the
  // name DESIGN.md's example uses) and `team` are required; `player` and `role`
  // are optional, and role defaults to "player".
  [[nodiscard]] static auto Load(const std::filesystem::path& path)
      -> Result<CsvRoster>;

  // An empty roster, for a game that needs no wristbands.
  CsvRoster() = default;

  // An unknown uid comes back as a Tag with an empty team, never as an error:
  // an unenrolled wristband is a normal event at camp, and the mode decides
  // what to do about it.
  [[nodiscard]] auto Lookup(std::string_view uid) const -> Tag override;

  [[nodiscard]] auto Size() const -> std::size_t
  {
    return m_tags.size();
  }

 private:
  // Ordered, so nothing that walks the roster can depend on a hash.
  std::map<std::string, Tag, std::less<>> m_tags;
};

}  // namespace sg

#endif  // SG_ENGINE_CONFIG_ROSTER_HPP_
