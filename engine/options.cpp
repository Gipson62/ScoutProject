#include "engine/options.hpp"

#include <charconv>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include "engine/core/error.hpp"

namespace sg
{
namespace
{

constexpr std::string_view kUsage =
    R"(sgengine — game engine for the 24-hour game

  sgengine [options]

  --config <file>     TOML config: mode, seed, teams, points, parameters
                      (default: config/dev.toml)
  --session <file>    append-only event log; an existing one is replayed
                      (default: sessions/dev.jsonl)
  --web <directory>   static pages to serve (default: web)
  --port <number>     port to listen on (default: 8080)
  -h, --help          this text

The simulator is at http://localhost:<port>/ and the dashboard at
http://localhost:<port>/dashboard.html.
)";

[[nodiscard]] auto NeedsValue(std::string_view option,
                              std::span<const std::string_view> rest)
    -> Result<std::string_view>
{
  if (rest.empty())
  {
    return Fail(ErrorCode::kConfigMissingField,
                std::string{option} + " needs a value");
  }
  return rest.front();
}

}  // namespace

auto UsageText() -> std::string_view
{
  return kUsage;
}

auto ParseOptions(std::span<const std::string_view> arguments)
    -> Result<Options>
{
  Options options;

  for (std::size_t i = 0; i < arguments.size(); ++i)
  {
    const std::string_view argument = arguments[i];
    const std::span<const std::string_view> rest = arguments.subspan(i + 1);

    if (argument == "-h" || argument == "--help")
    {
      options.help = true;
      return options;
    }

    if (argument == "--config" || argument == "--session" ||
        argument == "--web" || argument == "--port")
    {
      const Result<std::string_view> value = NeedsValue(argument, rest);
      if (!value)
      {
        return std::unexpected(value.error());
      }
      ++i;

      if (argument == "--config")
      {
        options.config = *value;
      }
      else if (argument == "--session")
      {
        options.session = *value;
      }
      else if (argument == "--web")
      {
        options.web = *value;
      }
      else
      {
        std::uint32_t port = 0;
        const char* begin = value->data();
        const char* end = begin + value->size();
        const std::from_chars_result parsed = std::from_chars(begin, end, port);
        if (parsed.ec != std::errc{} || parsed.ptr != end || port == 0 ||
            port > 65535)
        {
          return Fail(ErrorCode::kConfigBadValue,
                      "not a port: " + std::string{*value});
        }
        options.port = static_cast<std::uint16_t>(port);
      }
      continue;
    }

    return Fail(ErrorCode::kConfigBadValue,
                "unknown option: " + std::string{argument});
  }

  return options;
}

}  // namespace sg
