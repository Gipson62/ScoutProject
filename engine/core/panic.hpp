#ifndef SG_ENGINE_CORE_PANIC_HPP_
#define SG_ENGINE_CORE_PANIC_HPP_

#include <source_location>
#include <string_view>

namespace sg
{

// Ends the process after printing `message` and `location` to stderr.
//
// Like Rust's panic!(), this is for a situation that cannot happen unless the
// code is wrong: a broken invariant, out of memory, or a dependency that would
// have thrown in a build that has no unwinder. It is never used for an expected
// failure — bad input, a missing file, a closed socket — which returns a Result
// instead. The default argument captures the caller's location, so neither
// SG_ASSERT nor a direct call has to pass one.
[[noreturn]] [[gnu::cold]] auto Panic(
    std::string_view message,
    std::source_location location = std::source_location::current()) -> void;

}  // namespace sg

// Checks an invariant. Unlike assert(), it stays active in release builds: at
// camp a wrong state that keeps running is worse than a process that restarts
// and replays its log.
#define SG_ASSERT(condition, message) \
  do                                  \
  {                                   \
    if (!(condition)) [[unlikely]]    \
    {                                 \
      ::sg::Panic(message);           \
    }                                 \
  } while (false)

#endif  // SG_ENGINE_CORE_PANIC_HPP_
