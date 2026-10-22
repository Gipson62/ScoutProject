#ifndef SG_ENGINE_OPTIONS_HPP_
#define SG_ENGINE_OPTIONS_HPP_

// The command line, parsed by hand: five options do not justify a dependency.

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#include "engine/core/error.hpp"

namespace sg
{

struct Options
{
  std::filesystem::path config = "config/dev.toml";
  std::filesystem::path session = "sessions/dev.jsonl";
  std::filesystem::path web = "web";
  std::uint16_t port = 8080;
  bool help = false;
};

// Parses the arguments after the program name. An unknown option, a missing
// value or a port outside 1–65535 is an error naming the offending argument,
// not a silently ignored typo.
[[nodiscard]] auto ParseOptions(std::span<const std::string_view> arguments)
    -> Result<Options>;

[[nodiscard]] auto UsageText() -> std::string_view;

}  // namespace sg

#endif  // SG_ENGINE_OPTIONS_HPP_
