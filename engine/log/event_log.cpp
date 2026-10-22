#include "engine/log/event_log.hpp"

#include <algorithm>
#include <boost/json.hpp>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#include "engine/core/error.hpp"
#include "engine/core/event.hpp"

namespace sg
{
namespace
{

// std::FILE and the POSIX file descriptor below are this file's boundary with
// the C library: the handle is owned by a unique_ptr from the moment it exists,
// and the descriptor is read, used and dropped inside SyncToDisk.
[[nodiscard]] auto SyncToDisk(std::FILE& file) -> Result<void>
{
  if (std::fflush(&file) != 0)
  {
    return Fail(ErrorCode::kLogWriteFailed,
                std::string{"fflush: "} + std::strerror(errno));
  }
#if defined(_WIN32)
  if (_commit(_fileno(&file)) != 0)
#else
  if (::fsync(::fileno(&file)) != 0)
#endif
  {
    return Fail(ErrorCode::kLogWriteFailed,
                std::string{"fsync: "} + std::strerror(errno));
  }
  return {};
}

[[nodiscard]] auto ReadWholeFile(const std::filesystem::path& path)
    -> Result<std::string>
{
  std::ifstream in(path, std::ios::binary);
  if (!in)
  {
    return Fail(ErrorCode::kLogOpenFailed, "cannot read " + path.string());
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  if (in.bad())
  {
    return Fail(ErrorCode::kLogOpenFailed,
                "error while reading " + path.string());
  }
  return buffer.str();
}

[[nodiscard]] auto IsBlank(std::string_view line) -> bool
{
  return line.find_first_not_of(" \t\r") == std::string_view::npos;
}

}  // namespace

auto EventLog::FileCloser::operator()(std::FILE* file) const -> void
{
  if (file != nullptr)
  {
    static_cast<void>(std::fclose(file));
  }
}

EventLog::EventLog(std::unique_ptr<std::FILE, FileCloser> file,
                   LoadedLog loaded)
    : m_file(std::move(file)),
      m_events(std::move(loaded.events)),
      m_warnings(std::move(loaded.warnings))
{
  for (const Event& event : m_events)
  {
    m_next_seq = std::max(m_next_seq, event.seq + 1);
  }
}

auto EventLog::Load(const std::filesystem::path& path) -> Result<LoadedLog>
{
  const Result<std::string> content = ReadWholeFile(path);
  if (!content)
  {
    return std::unexpected(content.error());
  }

  LoadedLog loaded;
  std::string_view rest{*content};
  std::size_t line_number = 0;

  while (!rest.empty())
  {
    ++line_number;
    const std::size_t newline = rest.find('\n');

    if (newline == std::string_view::npos)
    {
      // No terminating newline, so this is the tail of a write that never
      // finished: the power went out between the fwrite and the fsync. Skipping
      // it loses an event that was never acknowledged, which is the safe half
      // of the trade.
      loaded.warnings.push_back("line " + std::to_string(line_number) +
                                ": truncated last line, skipped (" +
                                std::to_string(rest.size()) + " bytes)");
      break;
    }

    const std::string_view line = rest.substr(0, newline);
    rest.remove_prefix(newline + 1);

    if (IsBlank(line))
    {
      return Fail(
          ErrorCode::kLogCorruptLine,
          path.string() + ":" + std::to_string(line_number) + ": blank line");
    }

    boost::system::error_code ec;
    const boost::json::value value = boost::json::parse(line, ec);
    if (ec)
    {
      return Fail(ErrorCode::kLogCorruptLine, path.string() + ":" +
                                                  std::to_string(line_number) +
                                                  ": " + ec.message());
    }
    const boost::json::object* object = value.if_object();
    if (object == nullptr)
    {
      return Fail(ErrorCode::kLogCorruptLine, path.string() + ":" +
                                                  std::to_string(line_number) +
                                                  ": not a JSON object");
    }

    Result<Event> event = EventFromJson(*object);
    if (!event)
    {
      return Fail(ErrorCode::kLogCorruptLine, path.string() + ":" +
                                                  std::to_string(line_number) +
                                                  ": " + event.error().message);
    }
    loaded.events.push_back(std::move(*event));
  }

  return loaded;
}

auto EventLog::Open(const std::filesystem::path& path) -> Result<EventLog>
{
  std::error_code ec;
  if (path.has_parent_path() && !path.parent_path().empty())
  {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
    {
      return Fail(
          ErrorCode::kLogOpenFailed,
          "cannot create " + path.parent_path().string() + ": " + ec.message());
    }
  }

  LoadedLog loaded;
  if (std::filesystem::exists(path))
  {
    Result<LoadedLog> existing = Load(path);
    if (!existing)
    {
      return std::unexpected(existing.error());
    }
    loaded = std::move(*existing);
  }

  std::unique_ptr<std::FILE, FileCloser> file(
      std::fopen(path.string().c_str(), "ab"));
  if (!file)
  {
    return Fail(ErrorCode::kLogOpenFailed,
                "cannot open " + path.string() +
                    " for appending: " + std::strerror(errno));
  }

  return EventLog(std::move(file), std::move(loaded));
}

auto EventLog::Append(Event event) -> Result<std::uint64_t>
{
  event.seq = m_next_seq;

  const std::string line = boost::json::serialize(ToJson(event)) + "\n";
  if (std::fwrite(line.data(), 1, line.size(), m_file.get()) != line.size())
  {
    return Fail(ErrorCode::kLogWriteFailed,
                std::string{"short write: "} + std::strerror(errno));
  }

  const Result<void> synced = SyncToDisk(*m_file);
  if (!synced)
  {
    return std::unexpected(synced.error());
  }

  // Only now is the event real, so only now does it get its number and its
  // place in memory.
  ++m_next_seq;
  m_events.push_back(std::move(event));
  return m_events.back().seq;
}

auto EventLog::ByTimeWithoutVoided() const -> std::vector<Event>
{
  // Lookup only: nothing iterates this set, so it cannot affect replay order.
  std::unordered_set<std::uint64_t> voided;
  for (const Event& event : m_events)
  {
    if (const Void* undo = std::get_if<Void>(&event.data))
    {
      voided.insert(undo->target);
    }
  }

  std::vector<Event> out;
  out.reserve(m_events.size());
  for (const Event& event : m_events)
  {
    // The Void itself is not replayed: its whole effect is the removal above.
    if (std::holds_alternative<Void>(event.data) || voided.contains(event.seq))
    {
      continue;
    }
    out.push_back(event);
  }

  // Ties go to the lower sequence number, never to luck: two taps recorded at
  // the same millisecond must replay in the same order on every run.
  std::ranges::stable_sort(out,
                           [](const Event& a, const Event& b)
                           {
                             return std::pair{a.t, a.seq} <
                                    std::pair{b.t, b.seq};
                           });
  return out;
}

}  // namespace sg
