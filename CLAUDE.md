# sgengine

Game engine for an electronic 24-hour game at a scout camp in July 2027. Eight RFID capture points
on trees report over LoRa to one hidden computer at the base, which keeps the official score. The
engine is event-sourced: every input is appended to a JSON-lines log before it is processed, and the
state is always recomputed by replaying that log through a Python mode script.

## Where things are

- `docs/DESIGN.md` — the full design, four sections (technical design, explained in pictures, box
  plan, code architecture). It is the **working copy**: when a decision changes the design, update
  it in the same commit as the code and say so in the commit body.
- `docs/CODING_GUIDELINES.md` — the rules. Read it before writing code; it wins over any snippet in
  `DESIGN.md`.
- `docs/PROGRESS.md` — milestone checklist with commit hashes, decisions log, next-session notes.
  Update it in a `docs(progress): ...` commit at the end of each milestone.
- `docs/DEPENDENCIES.md` — every pinned dependency version.
- `engine/{core,log,script,io,config}`, `web/`, `modes/`, `config/`, `tests/`. Namespace `sg`.

## Hard rules

- C++23. No exceptions: built with `-fno-exceptions`; errors are `sg::Result<T>` =
  `std::expected<T, sg::Error>`, `std::optional` for "may be absent". `sg::Panic` only for a broken
  invariant, never for bad input or a missing file.
- No raw pointers in any API or data member. `std::unique_ptr` to own, references / `std::span` /
  `std::string_view` / `sg::OptionalRef` to borrow. A raw pointer is allowed only inside the one
  `.cpp` that talks to a C library, wrapped in RAII immediately. `engine/core/optional_ref.hpp` is
  the sole exception.
- Explicit sized integers (`std::int32_t`, …). Not `int`/`unsigned`/`long`, except `std::size_t`,
  `std::ptrdiff_t`, `char`, `bool`, and `main`'s `int`.
- Trailing return types everywhere, including `auto main() -> int`.
- Naming: types/functions PascalCase, locals snake_case, members `m_`, statics `s_`, constants and
  enumerators `k`, plain-aggregate members bare snake_case, macros `SG_`. Guards
  `SG_<PATH>_<FILE>_HPP_`.
- Determinism: time only via `Clock`, randomness only via the seeded `sg::Random`, never
  `std::*_distribution`, never iterate unordered containers in game logic, timers ordered by
  (due, id).
- One Asio thread owns all game state. No mutex in the Core.
- Boost.JSON header-only (`<boost/json/src.hpp>` in exactly one `.cpp`), `BOOST_NO_EXCEPTIONS`
  defined with both `boost::throw_exception` overloads routed to `Panic`, `error_code` overloads of
  Asio and Beast only. toml++ with `TOML_EXCEPTIONS 0`. pocketpy **C API only** (`pocketpy.h`), never
  the pybind11 layer.
- New dependencies need a reason written in the commit body.

## Build and test

```sh
cmake --preset clang-debug && cmake --build --preset clang-debug
ctest --preset clang-debug --output-on-failure
cmake --preset gcc-debug   && cmake --build --preset gcc-debug      # before each last commit
clang-format-19 --dry-run --Werror $(git ls-files '*.hpp' '*.cpp')
```

Toolchains: GCC 14 + libstdc++ 14, Clang 19. Warnings are errors
(`-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror`). Iterate with Clang
Debug; build and test with **both** compilers before a milestone's last commit.

Run the engine:

```sh
./build/clang-debug/engine/sgengine --config config/dev.toml \
    --session sessions/dev.jsonl --web web --port 8080
```

## Commits

[Conventional Commits 1.0.0](https://www.conventionalcommits.org/en/v1.0.0/). One logical change per
commit, builds and passes tests on its own, ideally under 300 changed lines. Subject says what, body
says why. Tests land with the code they test. Never commit `sessions/` or build output.
