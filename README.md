# sgengine

`sgengine` is the game engine for an electronic 24-hour game played at a scout camp in July 2027:
eight capture points hang on trees, scouts tap them with RFID wristbands, and one hidden computer at
the base keeps the official score. The engine is event-sourced — every input is written to an
append-only JSON-lines log before it is processed, and the game state is always recomputed by
replaying that log through a Python mode script — so a crash, a late radio packet or a disputed
capture all resolve by replay. This repository holds the C++23 engine, the mode scripts, the static
web pages (simulator, dashboard) and the configuration; the full design lives in
[`docs/DESIGN.md`](docs/DESIGN.md) and the rules every contributor follows in
[`docs/CODING_GUIDELINES.md`](docs/CODING_GUIDELINES.md).
