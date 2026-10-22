# OpenSkiPro2

Timing and results of alpine ski events, written in C++17 / Qt 6.
The user interface follows **SkiPro 2.27**: if you know SkiPro, you know OpenSkiPro2.
The timing device is the **TAG Heuer CP 540** (THCOM08 protocol, RS232 or Ethernet).

## Features

| Area | What is available |
|---|---|
| Events | Numbered event slots (`Event001.osp` …) like SkiPro: open / create, duplicate, save a copy, restore, delete. **Import of SkiPro 2.x events** (`EventNNN.scdb` + `EventNNNEx.scdb`). |
| Parameters | General parameters, event type (FIS with race category / national, DH, SG, GS, SL, AC, 1–9 runs, up to 3 intermediate times, timing precision 1 … 1/1000, final ranking: total or best of runs), printing parameters (fonts, margins, grey rows, 8 logos with positioning), header (jury, technical data, course setters, forerunners, gates, weather). |
| Competitors | Grid with search, New (F1) / Modify (F2) / Delete, configurable fields and grid columns, import from CSV / TXT / XLSX with column mapping (add, replace, update by code), import from another event, duplicate, format a field, assign bibs (by points, name, draw), categories by gender and birth years (SkiPro `.cat` files can be loaded). |
| FIS list | Import of the FIS alpine points list (`ALFP<list><season>F.zip` from the FIS website or the folder with its files) into its own database shared by all events; list status like the 1st page of the FIS list (validity, min / max penalty per category, adders, F / Z values, max points); import of competitors from the list into an event (separate window, double click / Enter, duplicates by FIS code refused); update of the competitors of an event from a new list by FIS code with a choice of the fields; warning when the list is expired. |
| FIS results | Race points and penalty according to the FIS Points Rules (constants from the FIS list, applied penalty typed by the TD), Final ranking (points), Penalty calculation sheet, FIS XML result file (`<Nation><Codex>.xml`). Bibs according to ICR 621 (X best drawn / reversed, NC / NJC double draw, bibs to skip). |
| Start lists | Editable start list per run; generation by bib, by points, random draw; run 2+ from the ranking of the previous run with reversal of the N best (0 = no reversal). |
| Timing | Timing window as in SkiPro: numbers to follow, start / intermediate / finish grids, inquiries, records, event ranking, timing device paper. Automatic assignment of the impulses, locks (F1–F3), active / inactive passages, insert a passage (F5), number up / down (F7 / F8), DNS / DNF / DSQ (F9–F11, Ctrl+F9…F11), NPS not permitted to start (Shift+F9, Ctrl+Shift+F9), search (Ctrl+F), information / penalty (F12), quick penalties (Ctrl+F5…F8), false impulses (minimum times), impulse delays, forerunners, sounds on channels, two timing devices. |
| CP 540 | RS232 (2400 – 57600 bd, optional ACK flow control) or TCP (port 7000 or 13500–13503), checksum check, recall of missing times (`#RT`), cancelled times (TC), numbers typed on the CP 540 keyboard per channel, timing over midnight. |
| Simulator | Built-in CP 540 simulator (manual impulses, automatic runs, lost frames, replay of a recorded log) — test without hardware. |
| Results | Run ranking, ranking after a run, final ranking, ties exactly as SkiPro, non-classified groups (DNS / NPS / DNF / DSQ per run). Manage runs page with counters, Modify data (manual times), manage un-processed, delete data. |
| Printing | List of competitors (with points), start lists, run ranking, ranking after a run, final ranking, times records, timing device paper. Document format per list (fields, alignment, width, columns, notes, signatures), preview, print, PDF, CSV. |
| API | Read-only REST + WebSocket API for other applications (scoreboards, live results, screens, TV graphics): event, competitors, start lists, results, live timing with intermediates and corrections pushed as they happen. API docs with *Try it out* (Swagger UI at `/docs`), OpenAPI description and a test page served by OpenSkiPro2 itself. |
| Languages | English; every text is translatable with Qt Linguist (see below). |

Not implemented yet (by decision): download of the FIS list, rankings by category,
display boards (will be solved differently), nordic / freestyle / snowboard,
parallel and ski cross, championships, teams.

## Download

Windows builds (64-bit) are on the [Releases](https://github.com/MartinSlachta/OpenSkiPro2/releases) page:
unzip `OpenSkiPro2-<version>-win64.zip` anywhere and start `OpenSkiPro2.exe`, no installation needed.
The program is not signed, so Windows SmartScreen may warn the first time (*More info › Run anyway*).

> **Test versions.** OpenSkiPro2 has not yet been used for a real FIS race. Try it with the
> built-in CP 540 simulator or alongside your usual timing, not as the only timing of a race.
> Please report problems as [issues](https://github.com/MartinSlachta/OpenSkiPro2/issues).

## Build

Requirements: Qt 6.8 or newer (tested with 6.8.3, MinGW 64-bit kit) with the modules Core, Gui,
Widgets, Sql (SQLite), SerialPort, Network, PrintSupport, HttpServer, WebSockets and LinguistTools;
CMake ≥ 3.21; Ninja.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64
cmake --build build
windeployqt --release --no-translations build/OpenSkiPro2.exe
```

## Quick start

1. *Local configuration › Folders*: folders for events, logos and exports.
2. *Local configuration › Timing Device 1*: `TAG HEUER CP 540`, serial port or IP, channels
   (default start 1, inter 2/3, finish 4). Choose *CP 540 simulator* to try without hardware.
3. *Files › Open an Event*, pick an empty slot to create an event — or
   *Files › Manage Events › Import a SkiPro Event…* to take over an existing SkiPro event.
4. *Event type*, *General parameters*, *Header*, competitors (*Manage Competitors › Import*).
5. *Manage Runs › Start List*, then *Timing*.
6. Lists and rankings in the *Edit* group.

## How the timing works

* Start impulse → the next competitor of the start list who has neither started nor got a status.
* Intermediate / finish impulse → the competitor on course who started first.
* A locked point (red ball, F1–F3) records the impulse without a number (warning icon); double-click
  a record to give it a number. With *Ask for bib if it is unknown* the program asks immediately.
* Number up / down moves the number of the selected record to the previous / next competitor of the
  start list and swaps it with the record that had that number (overtaking at the finish).
* Times are truncated, never rounded. Ranking order uses the full precision (1/1000 s); equal shown
  times share the rank; identical times: higher bib first (as SkiPro). The total of runs is the sum
  of the shown run times.
* Results are always recomputed from the time records, except results entered in *Modify Data*
  (button *Back to the times records* to undo).
* Every impulse is written to the event file immediately.

## Event file

An event is one SQLite file (`*.osp`): tables `params`, `competitors`, `start_list`,
`time_records` (raw impulses, time of day in µs), `time_infos` (results, ms), `paper`,
`run_meta`, `report_formats`. See `src/db/EventDb.cpp`.

## Translations

English is the source language. To add a language, e.g. Slovak:

1. add `translations/openskipro2_sk.ts` to `TS_FILES` in `CMakeLists.txt`,
2. `cmake --build build --target update_translations`,
3. translate the file with Qt Linguist, rebuild.

The language appears automatically in *Local configuration* (applied after a restart).

## Rules and specifications

The FIS functions follow these documents:

- FIS International Competition Rules (ICR) Alpine Skiing, 04.09.2026 (start order, draw, art. 620 / 621),
- FIS Points Rules Alpine Skiing, edition 2026/27 (race points, penalty calculation),
- FIS Alpine Data Booklet 1.16 with appendix, 30.09.2026 (FIS points list, documents, header),
- FIS Alpine Result XML Description 2.12, 10.09.2026 (FIS XML result file).

## Authors

| Role | Who |
|---|---|
| Coder | [Claude](https://claude.com/claude-code) (Anthropic) |
| Tester | Martin Slachta |

## License

OpenSkiPro2 is free software: you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation, version 3 of the
License (GPL-3.0). See [LICENSE](LICENSE). It uses Qt under the terms of the GNU LGPL v3 and embeds Swagger UI (Apache-2.0, `src/api/swagger`) for the API docs.
