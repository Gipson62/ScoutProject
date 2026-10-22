#ifndef SG_ENGINE_CORE_TOOLCHAIN_HPP_
#define SG_ENGINE_CORE_TOOLCHAIN_HPP_

#include <cstdint>
#include <string>

namespace sg
{

// Names the toolchain the engine was built with, for the --version line and for
// bug reports from camp: the same source must behave identically under GCC and
// Clang, so a report has to say which one produced the binary.
[[nodiscard]] auto ToolchainName() -> std::string;

// Evaluates one expression in the embedded interpreter and returns it as text.
// It exists so a build proves, before any engine code depends on it, that
// pocketpy's C API links and runs in a binary compiled with -fno-exceptions.
[[nodiscard]] auto RunEmbeddedScript(const std::string& source) -> std::string;

// Parses a TOML fragment and returns the integer at `key`, or 0 when the
// fragment is invalid or the key is absent. Same purpose, for toml++ with
// TOML_EXCEPTIONS off.
[[nodiscard]] auto ParseTomlInteger(const std::string& text,
                                    const std::string& key) -> std::int64_t;

}  // namespace sg

#endif  // SG_ENGINE_CORE_TOOLCHAIN_HPP_
