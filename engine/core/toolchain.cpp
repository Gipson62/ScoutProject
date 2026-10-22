#include "engine/core/toolchain.hpp"

#include <format>
#include <string>
#include <toml++/toml.hpp>

// pocketpy is a C library: its header is the only place a raw pointer appears
// in this file, and nothing from it is stored.
extern "C"
{
#include <pocketpy.h>
}

namespace sg
{

auto ToolchainName() -> std::string
{
#if defined(__clang__)
  return std::format("clang {}.{}.{}", __clang_major__, __clang_minor__,
                     __clang_patchlevel__);
#elif defined(__GNUC__)
  return std::format("gcc {}.{}.{}", __GNUC__, __GNUC_MINOR__,
                     __GNUC_PATCHLEVEL__);
#else
  return "unknown";
#endif
}

auto RunEmbeddedScript(const std::string& source) -> std::string
{
  py_initialize();
  std::string out;
  if (py_exec(source.c_str(), "<toolchain>", EVAL_MODE, nullptr))
  {
    out = py_tostr(py_retval());
  }
  py_finalize();
  return out;
}

auto ParseTomlInteger(const std::string& text, const std::string& key)
    -> std::int64_t
{
  const toml::parse_result parsed = toml::parse(text);
  if (!parsed)
  {
    return 0;
  }
  return parsed.table()[key].value_or(std::int64_t{0});
}

}  // namespace sg
