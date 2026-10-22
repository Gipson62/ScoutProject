#ifndef SG_ENGINE_CORE_COMPILER_HPP_
#define SG_ENGINE_CORE_COMPILER_HPP_

// The one place in the engine allowed to name a compiler builtin. Everything
// else uses the standard spelling, and GNU attributes are written in standard
// syntax ([[gnu::cold]]) so a compiler that does not know them ignores them.

#include <version>

#if __cpp_lib_unreachable >= 202202L
#include <utility>
#endif

namespace sg
{

// Marks a branch the code cannot reach. Reaching it is undefined behaviour, so
// it is used only where every case is already handled and the compiler cannot
// see it, never as an error path: a reachable failure returns a Result.
[[noreturn]] inline auto Unreachable() -> void
{
#if __cpp_lib_unreachable >= 202202L
  std::unreachable();
#else
  __builtin_unreachable();
#endif
}

}  // namespace sg

#endif  // SG_ENGINE_CORE_COMPILER_HPP_
