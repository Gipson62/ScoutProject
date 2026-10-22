# Progress

Tick each item with the hash of the commit that completed it. Update this file in a
`docs(progress): ...` commit at the end of each milestone. When a decision changes the design,
update `docs/DESIGN.md` in the same commit and say so in the commit body.

## Phase 1 — local prototype, no hardware

Design: `docs/DESIGN.md`, "Code architecture" section 1.

| Done | Milestone | Commit |
| --- | --- | --- |
| [ ] | M0 Guidelines, tooling, CMake skeleton, CI | |
| [ ] | M1 `feat(core)` vocabulary: `Error`, `Result`, `Panic`, `SG_ASSERT`, `OptionalRef`, `Millis`, `Event`, `Command`, JSON both ways | |
| [ ] | M2 `feat(log)` `EventLog`: append one JSON line per event, flush + fsync, sequence numbers, `Load` with per-line errors and a skipped truncated last line | |
| [ ] | M3 `feat(core)` `Clock` and `Scheduler`: steady and fake clocks, `After`, `Every`, `Cancel`, order by (due, id) | |
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

## Next session

Nothing yet: this file is created with the design import, before any engine code exists.
