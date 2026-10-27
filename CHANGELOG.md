# Changelog

OpenSkiPro2 uses version numbers 0.x until it has timed its first real FIS race
(minor version = a batch of features, patch version = fixes).

## 0.7.5 – 2026-10-27

* Timing: a competitor who started is put into the start list (by the start time),
  also when the number came from the CP540 with the impulse or was given to the start
  impulse later. Before, such a run had results but an empty start list (printed with
  0 competitors, while *Manage Runs* counted them). Opening the Timing of a run puts in
  the competitors timed before.

## 0.7.4 – 2026-10-27

* Print layouts: a run without its own layout (start list, run ranking, ranking after
  run) takes the layout of the nearest run that has one instead of the default, so the
  columns set for run 1 are there in run 2 too. The columns of the run follow it: list
  points / time of the previous run in the start list, one "time (rank)" per run in the
  ranking after run.

## 0.7.3 – 2026-10-27

* Leaving the Timing warns about bibs of the runs that no competitor has (e.g. a bib
  sent by the CP540 that was never given out): they have no name in the results and the
  outputs. *Competitors...* keeps the Timing open and opens the competitor management.
* FIS XML export: the same bibs are in the list of things to check.

## 0.7.2 – 2026-10-27

* Ranking: a competitor is "Did Not Qualify" for a run only when that run has a start
  list. A later run without one (not made yet, or cleared) leaves them not processed,
  so the total after that run no longer lists the whole field as DNQ (Publish showed
  it, the outputs got no classified rows).

## 0.7.1 – 2026-10-25

* API: `groupRanks` – rank and gap inside every output group of the competitor in
  `runs/{r}/results`, `results` (also for every run: `runs[].groupRanks`) and in
  `thisRun` / `total` / `previous` of the live lists and `competitor` events, so the
  outputs (Scoreboard, Screen, Publish) show the rank in the category without a
  request per group.

## 0.7.0 – 2026-10-25

* Competitor Fields: new column **Public** – the competitor fields shown to the
  spectators (screens and results on the internet), in the order of the rows, with
  their labels as headers. Name is always public; Bib, transponder, points and the
  separate last / first name cannot be. Events without the choice show the active
  fields of the grid.
* API: `event.fields` (the public fields with their labels), `competitor.custom`.

## 0.6.0 – 2026-10-24

* General Parameters: **Publish token** of the race (results on the internet with
  OpenSkiPro Publish). It is stored in the event, so two races of one day can have their
  tokens entered in advance.
* API: `general.publish.token` (read by the Publish module of OpenSkiPro Connect).

## 0.5.0 – 2026-10-22

* General Parameters: FIS code in three fields – nation (the nation of the event unless
  typed), codex (4 digits, completed with zeros) and the TD number of the Header;
  codes of older events and of SkiPro / SkiAlp imports are split automatically.
* FIS XML, Referee Report and the foot of the reports build the code from the three
  parts. API: `general.codex` is the codex only, new `general.fis`.
* Manage Runs: context menu on the ranking (DSQ with the gate, change of the gate,
  remove the DSQ, Modify Data); a double click opens Modify Data.

## 0.4.0 – 2026-10-22

* Referee Report (FIS form "Report by the Referee" of a run).
* Status NPS – not permitted to start (ICR 628): timing window (Shift+F9, Ctrl+Shift+F9),
  Modify data, Manage un-processed, rankings, penalty, FIS XML and API.
* Manage un-processed asks to save the changes when closed without Ok.

## 0.3.3 – 2026-10-21

* API: time of day of the intermediates, UTC offset of the timing device clock.

## 0.3.2 – 2026-10-21

* API: competitor events for every run, also after the timing (e.g. a DSQ given later).

## 0.3.1 – 2026-10-21

* `osp-apicheck`: records the API messages and checks them against the REST resources.
* API: revision of the results fixed.

## 0.3.0 – 2026-10-21

* Read-only REST + WebSocket API for other applications (scoreboards, live results,
  screens, TV graphics).
* API documentation with Swagger UI at `/docs`, test page; API Server dialog with clickable
  addresses.

## 0.2.0 – 2026-10-21

* FIS XML result file.
* Weather and snow choices in the header, the rules used listed in About.
* Report layouts per run (start list, run ranking, ranking after a run).
* Output groups loaded from a file or another event, saved to a file.
* Timing window: one selected competitor, the target of DNS / DNF / DSQ.

## 0.1.0 – 2026-10-20

First version.

* Events in numbered slots like SkiPro, own `.osp` file (SQLite); import of SkiPro 2.x and
  SkiAlp Pro 11 events.
* Competitors: grid, import from CSV / TXT / XLSX / clipboard, Format a Field, categories.
* FIS points list: import into its own database, list status, import / update of competitors.
* Bibs according to ICR 621 (double draw NC / NJC), start lists, reversal of the best.
* Timing window like SkiPro with the TAG Heuer CP 540 (RS232 / TCP), two timing devices,
  up to 3 intermediates, numbers to follow, locks, comments, timing device paper,
  built-in CP 540 simulator.
* Rankings with ties like SkiPro, output groups, Manage un-processed.
* FIS race points and penalty, report header like the FIS data booklet.
* Reports: competitors, start lists, rankings, times records; preview, print, PDF, CSV.
* English user interface, translatable with Qt Linguist.
