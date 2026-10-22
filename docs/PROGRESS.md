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
| [ ] | M4 `feat(core)` `Core`: `OnInput`, `Tick`, `RunUntil`, `CommandRouter` with mute, `GameState`, `Rebuild`; live-feed and rebuild give identical state hashes | |
| [ ] | M5 `feat(config)` `Config` (toml++) and roster CSV; errors name file and line | |
| [ ] | M6 `feat(script)` `ScriptHost` on the pocketpy C API, `scoutgame` module, `modes/capture.py` | |
| [ ] | M7 `feat(io)` `WebServer` (Beast) and `SimTransport` | |
| [ ] | M8 `feat(web)` `web/sim.html` and `web/dashboard.html` | |
| [ ] | M9 `feat(app)` `main.cpp`, `config/dev.toml`, roster; replay on start | |

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

## Next session

Phase 1 continues at M4: `Core`, `CommandRouter`, `GameState` and `Rebuild`.

Known issues and notes:

- `engine/core/toolchain.cpp` exists only to prove the toolchain and the dependency pins. Once the
  real modules replace what it checks, fold its tests into theirs and delete it.
- `ninja` is not installed by default in a fresh container; `apt-get install ninja-build` before the
  first configure. The CI workflow installs it.

Questions for the user: none open.
