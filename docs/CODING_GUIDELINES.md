# Coding guidelines

Base: the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html), with the
overrides and additions below. Where this file and Google disagree, this file wins. Where a code
snippet in `docs/DESIGN.md` and this file disagree, this file wins: the snippets in the design
document are illustrative.

## 1. Language and toolchains

**Standard:** C++23 (`CMAKE_CXX_STANDARD 23`, `CMAKE_CXX_STANDARD_REQUIRED ON`,
`CMAKE_CXX_EXTENSIONS OFF`). `std::expected`, which every fallible function returns, is a C++23
feature.

**Minimum toolchains:** GCC 14 with libstdc++ 14, and Clang 19 with libstdc++ 14 or libc++ 19.
Every commit compiles without warnings on both, with
`-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror`.

**C++23 features** are used where they help: `std::expected` with `and_then`, `transform` and
`or_else`; `std::print` and `std::println`; `std::to_underlying`; `std::ranges::to`; deducing
`this` where it removes a duplicated const and non-const overload pair.

```cpp
// good: one body instead of two
class Roster
{
  public:
    template <typename Self>
    auto Teams(this Self&& self) -> decltype(auto)
    {
        return std::forward<Self>(self).m_teams;
    }
};
```

```cpp
// bad: the same body written twice
class Roster
{
  public:
    auto Teams() -> std::vector<Team>& { return m_teams; }
    auto Teams() const -> const std::vector<Team>& { return m_teams; }
};
```

**C++26 features** are allowed only when both minimum toolchains support them, or behind one
compatibility header:

- `sg::OptionalRef<T>` in `engine/core/optional_ref.hpp` is `std::optional<T&>` (P2988) when the
  standard library has it, detected with the feature-test macro from `<version>`, and otherwise a
  small in-repo class with the same API subset. That header is the only place in the repository
  allowed to store a raw pointer.
- The placeholder variable `_` (P2169) is allowed; GCC 14 and Clang 18 implement it.
- Not allowed: static reflection, contracts, or anything only one of the two compilers implements.

**MSVC** is not a target, but nothing may make it impossible at no cost:

- GNU attributes are written in standard syntax: `[[gnu::cold]]`, `[[gnu::always_inline]]`,
  `[[gnu::format(printf, 1, 2)]]`. GCC and Clang honour them; other compilers ignore attributes
  they do not know.
- Compiler builtins live in `engine/core/compiler.hpp` and nowhere else.
- No statement expressions, no variable-length arrays, no `#embed`.

```cpp
[[noreturn]] [[gnu::cold]] auto Panic(std::string_view message) -> void;   // good
```

```cpp
__attribute__((noreturn, cold)) void Panic(std::string_view message);     // bad
```

## 2. Formatting

`.clang-format` at the repository root is the single source of truth, and CI fails on any file it
would reformat:

```yaml
BasedOnStyle: Google
BreakBeforeBraces: Allman
InsertBraces: true
AllowShortIfStatementsOnASingleLine: Never
AllowShortLoopsOnASingleLine: false
AllowShortBlocksOnASingleLine: Never
AllowShortFunctionsOnASingleLine: Empty
AllowShortLambdasOnASingleLine: Empty
DerivePointerAlignment: false
PointerAlignment: Left
```

Google's indentation and column limit are kept. Every `if`, `else`, `for` and `while` body is in
braces on its own lines, so any branch can be commented out without touching its neighbours.

```cpp
// good
if (timer.due <= now)
{
    Fire(timer);
}
else
{
    Requeue(timer);
}
```

```cpp
// bad: no braces, body on the condition's line
if (timer.due <= now) Fire(timer);
else Requeue(timer);
```

## 3. Trailing return types

Every function and member function, including `main`, declares its return type after the parameter
list. Enforced by clang-tidy's `modernize-use-trailing-return-type`.

```cpp
auto main() -> int;                                        // good
[[nodiscard]] auto Append(const Event& event) -> Result<std::uint64_t>;
```

```cpp
int main();                                                // bad
Result<std::uint64_t> Append(const Event& event);          // bad
```

## 4. Naming

Google's naming, with game-engine member prefixes. Enforced by clang-tidy's
`readability-identifier-naming`.

| Entity | Style | Example |
| --- | --- | --- |
| Types, enums, concepts | PascalCase | `EventLog` |
| Functions and methods | PascalCase | `RunUntil()` |
| Local variables, parameters | snake_case | `due_time` |
| Class data members | `m_` + snake_case | `m_timers` |
| Static data members | `s_` + snake_case | `s_next_id` |
| Constants and enumerators | `k` + PascalCase | `kMaxRetries`, `Pattern::kPulse` |
| Members of plain aggregate structs (all public, no invariants) | snake_case, no prefix | `Tap::point` |
| Namespaces | snake_case | `sg` |
| Macros | `SG_` + UPPER_CASE | `SG_ASSERT` |
| Files | snake_case `.hpp` / `.cpp` | `event_log.cpp` |

Header guards follow Google: `SG_<PATH>_<FILE>_HPP_`.

```cpp
// good: engine/core/event_log.hpp
#ifndef SG_ENGINE_CORE_EVENT_LOG_HPP_
#define SG_ENGINE_CORE_EVENT_LOG_HPP_

namespace sg
{
constexpr std::int32_t kMaxRetries = 3;

struct Tap                                 // plain aggregate: no prefix
{
    std::string point;
    std::string uid;
};

class EventLog                              // invariants: m_ prefix
{
  private:
    std::vector<Event> m_events;
    static std::uint64_t s_instances;
};
}  // namespace sg

#endif  // SG_ENGINE_CORE_EVENT_LOG_HPP_
```

```cpp
// bad
#pragma once                                // not a Google header guard
namespace SG {
const int MAX_RETRIES = 3;                  // should be kMaxRetries
class event_log {                           // should be EventLog
    std::vector<Event> events_;             // trailing underscore, should be m_events
    void run_until(Millis t);               // should be RunUntil
};
}
```

## 5. Explicit sized integers

Use `std::int32_t`, `std::uint8_t`, `std::int64_t` and so on. Never `int`, `unsigned`, `long` or
`short`. The exceptions: `std::size_t` and `std::ptrdiff_t` for sizes and indices that come from
standard containers, `char` for text, `bool`, and the `int` that `main` must return.

```cpp
auto Blend(std::uint8_t level, std::int32_t steps) -> std::uint8_t;   // good
for (std::size_t i = 0; i < points.size(); ++i) { ... }               // good
```

```cpp
unsigned char Blend(unsigned level, int steps);                       // bad
for (int i = 0; i < (int)points.size(); ++i) { ... }                  // bad
```

## 6. No raw pointers

Raw pointers appear in no API and no data member of ours.

- **Ownership:** `std::unique_ptr` by default; `std::shared_ptr` only with a comment giving the
  reason.
- **Non-owning:** references, `std::span`, `std::string_view`, `sg::OptionalRef<T>`.
- **C boundaries** (pocketpy, POSIX): a raw pointer is allowed inside the one `.cpp` file that
  talks to that library, converted immediately into RAII (a `std::unique_ptr` with a custom
  deleter) or a reference, and never stored or passed further.
- No `new`, no `delete`, no `malloc`, no `free`, no pointer arithmetic outside `std::span`.

```cpp
// good: owning handle to a C object, built once at the boundary
using VmHandle = std::unique_ptr<py_VM, decltype([](py_VM* vm) { py_delete_vm(vm); })>;

auto Sum(std::span<const std::int32_t> values) -> std::int64_t;    // good: non-owning view
auto FindTeam(std::string_view name) -> OptionalRef<Team>;         // good: may be absent
```

```cpp
py_VM* m_vm;                                                       // bad: stored raw pointer
auto Sum(const std::int32_t* values, std::size_t count) -> std::int64_t;   // bad
auto FindTeam(std::string_view name) -> Team*;                     // bad: nullptr as "absent"
```

## 7. No exceptions: errors are values

The engine is compiled with `-fno-exceptions`.

`engine/core/error.hpp` holds the vocabulary:

```cpp
struct Error
{
    ErrorCode code;
    std::string message;
};

template <typename T>
using Result = std::expected<T, Error>;
```

One `enum class ErrorCode` per module. `std::optional` means "may be absent"; `Result` means "may
fail". No sentinel values, no bool-plus-out-parameter. `[[nodiscard]]` goes on every function
returning `Result`, `std::optional` or `OptionalRef`.

```cpp
// good
[[nodiscard]] auto Load(const std::filesystem::path& path) -> Result<Config>;

auto config = Load(path);
if (!config)
{
    std::println(stderr, "{}", config.error().message);
    return 1;
}
```

```cpp
// bad: exception, sentinel, out-parameter
Config Load(const std::filesystem::path& path);        // throws
int Load(const std::filesystem::path& path);           // -1 means failure
bool Load(const std::filesystem::path& path, Config& out);
```

Truly exceptional situations — a broken invariant, out of memory, a dependency that would have
thrown — call `sg::Panic`, declared `[[noreturn]] [[gnu::cold]]`. It prints the message and the
source location to stderr and calls `std::abort()`. Like Rust's `panic!()`, it is never used for an
expected failure such as bad input, a missing file or a closed socket. `SG_ASSERT(condition,
message)` calls it and stays active in release builds.

```cpp
// good: the invariant cannot be false unless the code is wrong
SG_ASSERT(!m_timers.empty(), "RunUntil popped a timer from an empty queue");
```

```cpp
// bad: a missing file is expected input, not a broken invariant
if (!std::filesystem::exists(path))
{
    Panic("config file not found");
}
```

Third-party libraries are configured not to throw:

- `BOOST_NO_EXCEPTIONS` is defined, and both `boost::throw_exception` overloads are implemented to
  call `sg::Panic`. Only the `error_code` overloads of Asio and Beast are used, and only
  non-throwing Boost.JSON calls: `parse(text, ec)`, `if_contains`, `to_number<T>(ec)`. A call that
  can throw is a bug.
- toml++ is compiled with `TOML_EXCEPTIONS 0`, so parsing returns a `toml::parse_result` that is
  checked.
- A Python error inside a mode script is a pocketpy error value, never a C++ exception: log it with
  its traceback and skip that hook.

```cpp
// good
boost::system::error_code ec;
boost::json::value parsed = boost::json::parse(line, ec);
if (ec)
{
    return std::unexpected(Error{ErrorCode::kBadJson, ec.message()});
}
if (const boost::json::value* point = parsed.as_object().if_contains("point"))
{
    ...
}
```

```cpp
// bad: both of these throw
boost::json::value parsed = boost::json::parse(line);
std::int64_t seq = parsed.as_object().at("seq").to_number<std::int64_t>();
```

## 8. Determinism

The engine replays its log, so the same inputs must produce the same state on every run and on
every standard library.

- Game code reads time only through the `Clock` and randomness only through a seeded `sg::Random`
  implemented in this repository (SplitMix64 or xoshiro256\*\*). `std::*_distribution` is never
  used: its output differs between standard libraries.
- No iteration over unordered containers in game logic.
- Timers are ordered by due time, then by timer id.

```cpp
// good: our own generator, our own mapping to a range
auto roll = m_random.NextInRange(0, 99);
for (const auto& [id, point] : m_points)      // std::map: ordered
{
    ...
}
```

```cpp
// bad
std::uniform_int_distribution<std::int32_t> dist(0, 99);   // libstdc++ != libc++
auto roll = dist(engine);
for (const auto& [id, point] : m_points_hash)              // unordered_map: order varies
{
    ...
}
```

## 9. Threads

One Asio thread runs everything that touches game state: transports, web server, Core and
pocketpy. There is no mutex in the Core. Work that must run elsewhere is posted back onto that
thread.

```cpp
boost::asio::post(m_io, [this, event] { m_core.OnInput(event); });   // good
```

```cpp
std::lock_guard lock(m_state_mutex);                                 // bad: no mutex in the Core
```

## 10. Commits

[Conventional Commits 1.0.0](https://www.conventionalcommits.org/en/v1.0.0/). Each commit is one
logical change that builds and passes the tests on its own, ideally under 300 changed lines so the
whole diff can be reviewed line by line. The subject says what changed; the body says why. Tests
arrive with the code they test, in the same commit or the next one.
