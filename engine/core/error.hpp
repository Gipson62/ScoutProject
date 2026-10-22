#ifndef SG_ENGINE_CORE_ERROR_HPP_
#define SG_ENGINE_CORE_ERROR_HPP_

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace sg
{

// One block of enumerators per module, each with its own hundred, so a code
// read from a log line says which module produced it. A module's block is added
// in the commit that adds the module.
enum class ErrorCode : std::uint16_t
{
  kNone = 0,

  // core: the Event and Command vocabulary
  kMissingField = 100,
  kWrongType = 101,
  kUnknownKind = 102,
  kBadValue = 103,

  // log: the session file
  kLogOpenFailed = 200,
  kLogWriteFailed = 201,
  kLogCorruptLine = 202,

  // config: the TOML config file and the roster CSV
  kConfigOpenFailed = 300,
  kConfigParseFailed = 301,
  kConfigMissingField = 302,
  kConfigBadValue = 303,
  kRosterOpenFailed = 310,
  kRosterParseFailed = 311,
};

struct Error
{
  ErrorCode code;
  std::string message;
};

// "May fail". std::optional is "may be absent"; neither is a sentinel value and
// neither is an exception.
template <typename T>
using Result = std::expected<T, Error>;

// Shorthand for the failure branch: `return Fail(ErrorCode::kMissingField,
// ...)` converts to any Result<T>.
[[nodiscard]] inline auto Fail(ErrorCode code, std::string message)
    -> std::unexpected<Error>
{
  return std::unexpected(Error{code, std::move(message)});
}

// Short, stable text for a code, for logs and for the dashboard.
[[nodiscard]] auto Describe(ErrorCode code) -> std::string_view;

}  // namespace sg

#endif  // SG_ENGINE_CORE_ERROR_HPP_
