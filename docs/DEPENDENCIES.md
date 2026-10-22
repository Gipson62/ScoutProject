# Dependencies

Every version this repository pins. Change a pin only in a commit whose body gives the reason, and
update this table in the same commit.

## Toolchains

| Tool | Minimum | Used in this session | Why |
| --- | --- | --- | --- |
| GCC | 14 (libstdc++ 14) | 14.2.0 | `std::print`, `std::expected`, the placeholder variable `_` |
| Clang | 19 (libstdc++ 14 or libc++ 19) | 19.1.1 | Second toolchain: two front ends catch what one accepts |
| CMake | 3.25 | 3.28.3 | `PROJECT_IS_TOP_LEVEL`, presets v6, `SYSTEM` on `FetchContent_Declare` |
| Ninja | — | 1.11.1 | Generator named by every preset |
| clang-format | 19 | 19.1.1 | `InsertBraces` and the Allman rules of `.clang-format` |
| clang-tidy | 19 | 19.1.1 | `readability-identifier-naming` options used by `.clang-tidy` |

## Libraries

| Library | Version | How it arrives | Notes |
| --- | --- | --- | --- |
| Boost | ≥ 1.83 (1.83.0 here) | System package (`libboost-dev`, `libboost-json-dev`) | Asio, Beast and JSON, all header-only. Boost.JSON is compiled in through `<boost/json/src.hpp>` in exactly one translation unit. `BOOST_NO_EXCEPTIONS` is defined and both `boost::throw_exception` overloads call `sg::Panic`. |
| toml++ | `v3.4.0` | CMake `FetchContent` | Single header. Built with `TOML_EXCEPTIONS=0`, so parsing returns a `toml::parse_result`. |
| pocketpy | `v2.2.0` | CMake `FetchContent`, `PK_BUILD_STATIC_LIB=ON` | **C API only** (`pocketpy.h`). The pybind11-compatible layer is not used: its design relies on C++ exceptions, and the engine is built with `-fno-exceptions`. |
| GoogleTest | `v1.17.0` | CMake `FetchContent`, only when `BUILD_TESTING` | Not linked into the engine. |

The pins live in `cmake/Dependencies.cmake` as `SG_TOMLPLUSPLUS_TAG`, `SG_POCKETPY_TAG` and
`SG_GOOGLETEST_TAG`.

## Nothing else

A new dependency needs a reason written in the commit body that adds it. Deliberately absent so
far: miniaudio (audio is Phase 3), a CSV library (the roster reader is twenty lines), and any
JavaScript framework or build step (`web/` is plain HTML, CSS and JavaScript).
