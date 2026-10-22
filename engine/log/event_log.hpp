#ifndef SG_ENGINE_LOG_EVENT_LOG_HPP_
#define SG_ENGINE_LOG_EVENT_LOG_HPP_

// The append-only session log. It is the truth of a game: the state is only a
// cache recomputed by replaying these lines, so a line is on disk before
// anything acts on it.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "engine/core/error.hpp"
#include "engine/core/event.hpp"

namespace sg
{

// What reading a session file back produced. A warning is a line that was
// skipped without failing the load; see Load.
struct LoadedLog
{
  std::vector<Event> events;
  std::vector<std::string> warnings;
};

class EventLog
{
 public:
  // Opens `path` for appending, reading back whatever it already holds so
  // sequence numbers continue where the last run stopped. Creates the file, and
  // the directories above it, when they are missing.
  [[nodiscard]] static auto Open(const std::filesystem::path& path)
      -> Result<EventLog>;

  // Reads a session file without opening it for writing: for tests, for tools,
  // and for the dashboard's mirror of the log.
  //
  // A line that cannot be parsed is an Error naming its line number, because a
  // silently dropped event is a wrong score. The one exception is a last line
  // with no terminating newline: that is a write cut off by a power loss, so it
  // is skipped and reported in `warnings`.
  [[nodiscard]] static auto Load(const std::filesystem::path& path)
      -> Result<LoadedLog>;

  // Assigns the next sequence number, writes one JSON line, and flushes it to
  // the platter before returning it. Nothing in the engine may act on an event
  // that is not yet durable.
  [[nodiscard]] auto Append(Event event) -> Result<std::uint64_t>;

  [[nodiscard]] auto Events() const -> std::span<const Event>
  {
    return m_events;
  }
  [[nodiscard]] auto NextSeq() const -> std::uint64_t
  {
    return m_next_seq;
  }
  [[nodiscard]] auto Warnings() const -> std::span<const std::string>
  {
    return m_warnings;
  }

  // Replay order: by the time an event really happened, ties broken by sequence
  // number, with every event a Void targets removed along with the Void itself.
  // A late radio packet therefore takes its true place in the past.
  [[nodiscard]] auto ByTimeWithoutVoided() const -> std::vector<Event>;

 private:
  struct FileCloser
  {
    auto operator()(std::FILE* file) const -> void;
  };

  EventLog(std::unique_ptr<std::FILE, FileCloser> file, LoadedLog loaded);

  std::unique_ptr<std::FILE, FileCloser> m_file;
  std::vector<Event> m_events;
  std::vector<std::string> m_warnings;
  std::uint64_t m_next_seq = 1;
};

}  // namespace sg

#endif  // SG_ENGINE_LOG_EVENT_LOG_HPP_
