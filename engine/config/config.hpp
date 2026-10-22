#ifndef SG_ENGINE_CONFIG_CONFIG_HPP_
#define SG_ENGINE_CONFIG_CONFIG_HPP_

// The TOML config file: which mode to run, the seed, the teams, the capture
// points and the live-editable parameters. This is the layer DESIGN.md gives to
// "chiefs who edit text", so every error names the file and the line.

#include <filesystem>
#include <string>
#include <string_view>

#include "engine/core/error.hpp"
#include "engine/core/event.hpp"
#include "engine/core/game_state.hpp"

namespace sg
{

struct Config
{
  // modes/<mode>.py
  std::filesystem::path mode_path;
  // The roster CSV, resolved against the config file's own directory when the
  // path in the file is relative.
  std::filesystem::path roster_path;
  // What the Core starts a game from.
  Setup setup;
};

[[nodiscard]] auto LoadConfig(const std::filesystem::path& path)
    -> Result<Config>;

// "#e8432e", "e8432e" or one of the colour names a chief is likely to type.
// Separate from LoadConfig so a mode script can be given the same parser.
[[nodiscard]] auto ParseColour(std::string_view text) -> Result<Rgb>;

}  // namespace sg

#endif  // SG_ENGINE_CONFIG_CONFIG_HPP_
