#ifndef SG_ENGINE_SCRIPT_SCRIPT_HOST_HPP_
#define SG_ENGINE_SCRIPT_SCRIPT_HOST_HPP_

// Runs a mode script. This is the only part of the engine that knows pocketpy
// exists, and it knows it through the C API alone: the pybind11-compatible
// layer relies on C++ exceptions, which the engine is built without. The
// interpreter stays behind a pimpl so no other translation unit sees its
// headers or its raw pointers.

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "engine/core/event.hpp"
#include "engine/core/game_state.hpp"
#include "engine/core/mode.hpp"
#include "engine/core/scheduler.hpp"

namespace sg
{

class ScriptHost final : public Mode
{
 public:
  // `name` is what a traceback calls the file. Only one ScriptHost may exist at
  // a time: pocketpy's interpreter is a process-wide singleton, so creating a
  // second one panics.
  ScriptHost(std::string_view source, std::string_view name);
  ~ScriptHost() override;

  ScriptHost(const ScriptHost&) = delete;
  ScriptHost(ScriptHost&&) = delete;
  auto operator=(const ScriptHost&) -> ScriptHost& = delete;
  auto operator=(ScriptHost&&) -> ScriptHost& = delete;

  // The traceback of a script that would not load, if it did not. A host with a
  // load error runs no hooks at all; main refuses to start the engine on it,
  // rather than play a game whose rules never ran.
  [[nodiscard]] auto LoadError() const -> std::optional<std::string>;

  // Every hook error so far, newest last, each with its traceback. A failing
  // hook is skipped and the game continues, as DESIGN.md requires, so these are
  // what the dashboard shows instead.
  [[nodiscard]] auto Errors() const -> std::span<const std::string>;

  // Mode
  auto OnStart(GameState& state, ModeHost& host) -> void override;
  auto OnTag(GameState& state, ModeHost& host, std::string_view point,
             const Tag& tag) -> void override;
  auto OnButton(GameState& state, ModeHost& host, std::string_view point)
      -> void override;
  auto OnTimer(GameState& state, ModeHost& host, TimerId id) -> void override;

  // Defined in script_host.cpp, where pocketpy lives. Public so the file's own
  // binding functions can name it; nothing outside that file can, because its
  // definition is not in this header.
  struct Impl;

 private:
  std::unique_ptr<Impl> m_impl;
};

}  // namespace sg

#endif  // SG_ENGINE_SCRIPT_SCRIPT_HOST_HPP_
