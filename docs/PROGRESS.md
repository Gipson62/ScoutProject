# Progress

Tick each item with the hash of the commit that completed it. Update this file in a
`docs(progress): ...` commit at the end of each milestone. When a decision changes the design,
update `docs/DESIGN.md` in the same commit and say so in the commit body.

## Phase 1 — local prototype, no hardware

Design: `docs/DESIGN.md`, "Code architecture" section 1.

| Done | Milestone | Commit |
| --- | --- | --- |
| [x] | M0 Guidelines, tooling, CMake skeleton, CI | `f4ce93f` |
| [x] | M1 `feat(core)` vocabulary: `Error`, `Result`, `Panic`, `SG_ASSERT`, `OptionalRef`, `Millis`, `Event`, `Command`, JSON both ways | `73c3a83`, `06a6603`, `6e98ec0` |
| [x] | M2 `feat(log)` `EventLog`: append one JSON line per event, flush + fsync, sequence numbers, `Load` with per-line errors and a skipped truncated last line | `6473432` |
| [x] | M3 `feat(core)` `Clock` and `Scheduler`: steady and fake clocks, `After`, `Every`, `Cancel`, order by (due, id) | `850375b` |
| [x] | M4 `feat(core)` `Core`: `OnInput`, `Tick`, `RunUntil`, `CommandRouter` with mute, `GameState`, `Rebuild`; live-feed and rebuild give identical state hashes | `1d887c6`, `4e7b955` |
| [x] | M5 `feat(config)` `Config` (toml++) and roster CSV; errors name file and line | `3f854a4` |
| [x] | M6 `feat(script)` `ScriptHost` on the pocketpy C API, `scoutgame` module, `modes/capture.py` | `c5d82f2` |
| [x] | M7 `feat(io)` `WebServer` (Beast) and `SimTransport` | `d9ace3d` |
| [x] | M8 `feat(web)` `web/sim.html` and `web/dashboard.html` | `6f6c84c` |
| [x] | M9 `feat(app)` `main.cpp`, `config/dev.toml`, roster; replay on start | `75d317a` |

Out of Phase 1 scope, carried to Phase 3: typed panel actions, team panels, the full domination
mode, `Void` and undo, audio, UPS, health monitoring.

## Phase 2 — February prototype: three boxes, a hub and the Zero

Design: `docs/DESIGN.md`, "Code architecture" section 2.

| Done | Milestone | Commit |
| --- | --- | --- |
| [ ] | December range test with stock Meshtastic | |
| [ ] | `firmware/common/protocol.hpp` and a round-trip test | |
| [ ] | Node firmware: tag read, local LED, radio send | |
| [ ] | Hub firmware: radio ↔ UART JSON bridge | |
| [ ] | `SerialTransport`: age → true timestamp, de-duplication, reconnect | |
| [ ] | Dashboard shows heartbeats and battery | |
| [ ] | ACK, outbox and `LedState` round trip; measure tap → team colour | |
| [ ] | Hub unplugged 5 minutes: buffered taps arrive with their real times | |
| [ ] | Cross-compilation to Linux arm64 for the Zero | |

## Phase 3 — full architecture: the camp system

Design: `docs/DESIGN.md`, "Code architecture" sections 3 and 4.

| Done | Milestone | Commit |
| --- | --- | --- |
| [ ] | Typed actions (`ActionSpec`, `ParamSpec`) and the generic team panel | |
| [ ] | Station link: tag enrollment, panel opening by wristband, LED map | |
| [ ] | `Void` and undo, plus a timeline scrubber in the dashboard | |
| [ ] | Audio (miniaudio), UPS gauge, health alerts | |
| [ ] | `modes/domination.py` and `config/camp2027.toml`, played end to end in the simulator with six fake teams | |
| [ ] | Mode library: koth, bomb | |

## Decisions

| Date | Decision | Reason |
| --- | --- | --- |
| 2026-10-22 | `docs/DESIGN.md` is the working copy of the Claude Docs document; the document is not read again | One source of truth inside the repository, versioned with the code it describes |
| 2026-10-22 | The 15 drawings of the design document became Mermaid diagrams (flows, sequences, block diagrams) or short written descriptions, keeping their captions | The drawings are widgets that no export carries; a Mermaid diagram is text, so it diffs and is editable |
| 2026-10-22 | C++23, not the C++20 the design document's repository layout mentions | `std::expected` is the error-as-value vocabulary of the whole engine, and it is C++23 |
| 2026-10-22 | pocketpy through its **C API** (`pocketpy.h`), not the pybind11-compatible layer the design document sketches | pybind11's design relies on C++ exceptions, and the engine is built with `-fno-exceptions` |
| 2026-10-22 | Naming is PascalCase for functions and methods, `m_`/`s_`/`k` prefixes, which the design document's snippets do not follow | One naming scheme across the repository, enforced by `readability-identifier-naming` |
| 2026-10-22 | One `sg::ErrorCode` enum with a block of a hundred per module, instead of one enum per module | `Error` holds a single `code` field, as the plan specifies, so a per-module enum type would make `Error` and therefore `Result` generic in it; a block per module keeps both concrete and still says which module produced a code |
| 2026-10-22 | `Beep::kind` is a `BeepKind` enumerator, not the design's bare `std::uint8_t`, and serialises as `"ok"` / `"deny"` / `"alarm"` | The simulator and the dashboard would otherwise need a table of magic numbers; the wire form stays readable in a log line |
| 2026-10-22 | `MapLed::index` is `std::int32_t`, not the design's `int` | The explicit-sized-integer rule; `int` is not one of its exceptions |
| 2026-10-22 | The session log line is `{"seq","t","kind",…}`, discriminated by `"kind"`; a WebSocket frame is discriminated by `"type"` | DESIGN.md fixes the frame shapes but not the log shape; two different key names keep a log line and a frame impossible to confuse |
| 2026-10-22 | `EventLog::Open` reads the file back and continues its numbering, rather than starting a new file per run | "Restart the engine and the game resumes" is the whole point of the log; a second file would split one session in two |
| 2026-10-22 | A corrupt line anywhere but at the end fails the load; only an unterminated last line is skipped, with a warning | A line the parser cannot read is an event that happened and would silently vanish from the score. An unterminated last line is different: it was never acknowledged, so nothing downstream ever saw it |
| 2026-10-22 | `Scheduler::PopDue` requeues a repeating timer **before** returning it, where the design's `run_until` pushes the repeat after firing | A hook that cancels its own repeating timer would otherwise see it come back, since the push happened after the cancel |
| 2026-10-22 | A repeating timer that owes several firings fires all of them when time jumps | A replay must credit the same income as the live run; skipping to the last firing would pay less |
| 2026-10-22 | A `Mode` interface sits between the Core and the rules, where the design has the Core call `ScriptHost` directly | The Core is testable against a stub mode in C++, before and independently of pocketpy, and the interpreter stays swappable in one place, which is what the design wanted `ScriptHost` for. M6's `ScriptHost` is another `Mode` |
| 2026-10-22 | `Scheduler::Clear` restarts timer ids from 1 | A mode keeps its handles in `point.data`, so an id is part of the state; a rebuild that numbered timers differently reached a different state hash than the live run |
| 2026-10-22 | The engine assigns `LedState::version`, not the mode | A node reports the version it is showing in its heartbeat, so a mismatch is what triggers a resend; a script cannot be trusted to count |
| 2026-10-22 | `GameState::Hash` includes the random generator's position | Two states that agree on everything else but have drawn a different number of values diverge on the next roll, so they are not the same state |
| 2026-10-22 | The config accepts both `[points]` and `[nodes]`, and both `[[teams]]` and an inline `teams` array | DESIGN.md's two tabs use different spellings for the same tables, and both examples should load |
| 2026-10-22 | Points are sorted by id after loading | A TOML table has no order; point order is visible in the state snapshot and in `PushFullState`, so it must not depend on how the file was typed |
| 2026-10-22 | The roster CSV requires a header row; `uid` or `tag` names the wristband column, `player` and `role` are optional | A chief fills it in a spreadsheet, where a header row is free and column order is not worth depending on |
| 2026-10-22 | The `scoutgame` module is half Python (a prelude with the decorators, hook lists and dispatchers) and half C | Filtering `@on_tag` by point, team or role is three lines of Python and thirty of C; the C half is only what needs the engine |
| 2026-10-22 | pocketpy's interpreter is started once per process and wiped with `py_resetvm` for each later `ScriptHost` | `py_initialize` refuses to run again after `py_finalize`, and a wiped VM is what a rebuild wants anyway. `Core::Rebuild` destroys the old mode before creating the new one so two hosts never overlap |
| 2026-10-22 | Timer callables and their arguments are kept on the Python side, keyed by the engine's timer id | The handle a script stores in `point.data` is then the scheduler's own id, which a replay reproduces exactly |
| 2026-10-22 | Script data is mirrored into `GameState` as JSON after every hook; a `Team` or `Point` stored in it mirrors as `"team:<name>"` | The snapshot and its hash must see script data, and the mirror only has to tell two states apart — it is never read back into Python |
| 2026-10-22 | `game.random` is the engine's seeded generator, and pocketpy's own `random` is not exposed | A script must not be able to break replay determinism even by accident, which is what DESIGN.md asks of the script runtime |
| 2026-10-22 | Beast is included through `engine/io/beast.hpp`, which silences `-Wdeprecated-declarations` for those includes only | Beast 1.83 uses `std::aligned_storage`, deprecated in C++23, so `-Werror` stops the build on code we do not own; silencing it per-include keeps the warning live for ours |
| 2026-10-22 | One static page is served for `/`: `sim.html` | Phase 1's landing page is the simulator; the dashboard is a second page beside it |
| 2026-10-22 | Phase 1 has one `CommandSink`, `OutputSink`, routing LEDs and beeps to the transport and everything else to the browser | The station, audio and panels are Phase 3; the Core does not change when they arrive |
| 2026-10-22 | A page gets a whole `state` frame rather than reconstructing state from the small frames | A page that has to infer an owner from an announcement shows the wrong thing; the frame is about a kilobyte and the state is the engine's to state |
| 2026-10-22 | `ToJson` writes sequence numbers and LED versions as signed JSON numbers | A frame built in C++ and the same frame parsed back from a log line then hold the same JSON kinds and compare equal |
| 2026-10-22 | The dashboard is read-only in Phase 1 | Void, pause, live parameters and the timeline scrubber need every click to be an event in the log, which is Phase 3 |
| 2026-10-22 | On restart, game time resumes at the last logged event's time, and the time the engine was down is not credited | Game time is ms since the session started; beginning again at zero would make every new event look late and force a rebuild on each one. A five-minute countdown two minutes in has three minutes left from the restart |
| 2026-10-22 | A page is told it connected only once its WebSocket handshake has finished | The welcome frame was otherwise written to a stream that could not take it, and the page sat empty until the next event |
| 2026-10-22 | The command line is parsed by hand in `engine/options.cpp` | Five options do not justify a dependency, and the parser is unit-tested |
| 2026-10-22 | A session log that cannot be written stops the engine | Every later replay would be wrong, and the engine's whole promise is that the log is the truth |

## Next session

Phase 1 is complete: the engine runs, replays its log and is driven from the browser. Phase 2
(firmware, `SerialTransport`, cross-compilation to the Zero) is deliberately untouched.

Start here:

1. Read `CLAUDE.md`, then this file, then the "Code architecture" section of `docs/DESIGN.md`.
2. `apt-get install -y g++-14 clang-19 clang-format-19 clang-tidy-19 ninja-build libboost-dev
   libboost-json-dev libboost-system-dev` — a fresh container has none of them.
3. `cmake --preset clang-debug && cmake --build --preset clang-debug && ctest --preset clang-debug`

Known issues and loose ends:

- `engine/core/toolchain.cpp` exists only to prove the compilers and the dependency pins. Its three
  checks are now covered by the real modules' own tests, so it can be deleted whenever a commit has
  a reason to touch that area.
- clang-tidy has a configuration (`.clang-tidy`) but nothing runs it: CI builds, tests and checks
  formatting only. Adding a clang-tidy job needs `compile_commands.json`, which the presets already
  produce, and will probably turn up findings in the pocketpy boundary code.
- `Void` is logged and honoured by `Rebuild`, but nothing produces one yet: the dashboard is
  read-only until Phase 3.
- `modes/capture.py` needs `capture_minutes` in `[params]`; a config without it fails the hook with
  an AttributeError, visible in the engine's error list but not before the game starts. A mode
  parameter check at load time would be worth having.
- The simulator shows beeps as a flash of the dome rather than playing a sound.
- The 100 ms tick broadcasts a state frame only when the state hash changed, so an idle game sends
  nothing; with the domination mode's per-minute income it will send one frame a minute, which is
  fine, but a mode that changes state every tick would make it chatty.

Questions for the user:

1. `config/dev.toml` keeps `capture_minutes = 5`, which makes a manual run slow to watch. Would a
   second file (`config/dev-fast.toml`, 10 seconds) be useful, or is editing the one file fine?
2. Phase 2 needs the real `firmware/` tree and a cross-compilation toolchain file for the Zero. Is
   the hardware (XIAO kits, RC522 readers) already to hand, or should Phase 2 start with the
   protocol header and its round-trip test alone?
