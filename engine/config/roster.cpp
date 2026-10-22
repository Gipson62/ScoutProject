#include "engine/config/roster.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "engine/core/error.hpp"
#include "engine/core/mode.hpp"

namespace sg
{
namespace
{

[[nodiscard]] auto Trim(std::string_view text) -> std::string_view
{
  while (!text.empty() &&
         (std::isspace(static_cast<unsigned char>(text.front())) != 0))
  {
    text.remove_prefix(1);
  }
  while (!text.empty() &&
         (std::isspace(static_cast<unsigned char>(text.back())) != 0))
  {
    text.remove_suffix(1);
  }
  return text;
}

[[nodiscard]] auto SplitFields(std::string_view line)
    -> std::vector<std::string>
{
  std::vector<std::string> fields;
  std::size_t start = 0;
  while (true)
  {
    const std::size_t comma = line.find(',', start);
    const std::string_view field = comma == std::string_view::npos
                                       ? line.substr(start)
                                       : line.substr(start, comma - start);
    fields.emplace_back(Trim(field));
    if (comma == std::string_view::npos)
    {
      return fields;
    }
    start = comma + 1;
  }
}

[[nodiscard]] auto Lowered(std::string_view text) -> std::string
{
  std::string out{text};
  std::ranges::transform(out, out.begin(),
                         [](char c)
                         {
                           return static_cast<char>(std::tolower(c));
                         });
  return out;
}

// Which column holds what, read from the header row.
struct Columns
{
  std::optional<std::size_t> uid;
  std::optional<std::size_t> team;
  std::optional<std::size_t> player;
  std::optional<std::size_t> role;
};

[[nodiscard]] auto ReadHeader(const std::vector<std::string>& fields) -> Columns
{
  Columns columns;
  for (std::size_t i = 0; i < fields.size(); ++i)
  {
    const std::string name = Lowered(fields[i]);
    if (name == "uid" || name == "tag")
    {
      columns.uid = i;
    }
    else if (name == "team")
    {
      columns.team = i;
    }
    else if (name == "player" || name == "name")
    {
      columns.player = i;
    }
    else if (name == "role")
    {
      columns.role = i;
    }
  }
  return columns;
}

[[nodiscard]] auto Field(const std::vector<std::string>& fields,
                         const std::optional<std::size_t>& column)
    -> std::string
{
  if (!column.has_value() || *column >= fields.size())
  {
    return {};
  }
  return fields[*column];
}

}  // namespace

auto CsvRoster::Load(const std::filesystem::path& path) -> Result<CsvRoster>
{
  std::ifstream in(path, std::ios::binary);
  if (!in)
  {
    return Fail(ErrorCode::kRosterOpenFailed,
                "no such roster file: " + path.string());
  }

  const auto where = [&path](std::size_t line)
  {
    return path.string() + ":" + std::to_string(line) + ": ";
  };

  CsvRoster roster;
  Columns columns;
  bool header_seen = false;
  std::string line;
  std::size_t line_number = 0;

  while (std::getline(in, line))
  {
    ++line_number;
    if (!line.empty() && line.back() == '\r')  // a spreadsheet on Windows
    {
      line.pop_back();
    }
    const std::string_view trimmed = Trim(line);
    if (trimmed.empty() || trimmed.starts_with('#'))
    {
      continue;
    }

    const std::vector<std::string> fields = SplitFields(trimmed);
    if (!header_seen)
    {
      header_seen = true;
      columns = ReadHeader(fields);
      if (!columns.uid.has_value() || !columns.team.has_value())
      {
        return Fail(ErrorCode::kRosterParseFailed,
                    where(line_number) +
                        "the header row needs a uid (or tag) column and a team "
                        "column");
      }
      continue;
    }

    Tag tag;
    tag.uid = Field(fields, columns.uid);
    tag.team = Field(fields, columns.team);
    tag.player = Field(fields, columns.player);
    tag.role = Field(fields, columns.role);
    if (tag.role.empty())
    {
      tag.role = "player";
    }

    if (tag.uid.empty())
    {
      return Fail(ErrorCode::kRosterParseFailed,
                  where(line_number) + "empty uid");
    }
    if (tag.team.empty())
    {
      return Fail(ErrorCode::kRosterParseFailed,
                  where(line_number) + "wristband " + tag.uid + " has no team");
    }
    // A wristband that names two teams would make a tap ambiguous, so it is an
    // error here rather than a surprise during the game.
    if (const auto existing = roster.m_tags.find(tag.uid);
        existing != roster.m_tags.end())
    {
      return Fail(ErrorCode::kRosterParseFailed,
                  where(line_number) + "wristband " + tag.uid +
                      " is already enrolled for " + existing->second.team);
    }
    roster.m_tags.emplace(tag.uid, std::move(tag));
  }

  if (!header_seen)
  {
    return Fail(ErrorCode::kRosterParseFailed,
                path.string() + ": the file holds no header row");
  }
  return roster;
}

auto CsvRoster::Lookup(std::string_view uid) const -> Tag
{
  const auto found = m_tags.find(uid);
  if (found == m_tags.end())
  {
    return Tag{std::string{uid}, "", "", ""};
  }
  return found->second;
}

}  // namespace sg
