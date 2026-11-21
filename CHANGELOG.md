# Changelog

OpenSkiPro2 uses version numbers 0.x until it has timed its first real FIS race
(minor version = a batch of features, patch version = fixes).

## 0.11.3 – 2026-11-21

* Timing: a number of a record changed or removed by the operator is written into the
  comment of the record.
* Software Parameters, Timing Options: *Start/Ints/Finish locked* - the mode of all the
  points (locked = numbers from the timing device, otherwise automatic) when the timing
  opens or the option changes; F1 / F2 / F3 and the context menu still switch it.

## 0.11.2 – 2026-11-21

* Timing: a forerunner number sent by the timing device is not put into the start list
  any more, and an unlocked point keeps it (only a typed number replaces it).

## 0.11.1 – 2026-11-21

* Timing, automatic lock: a point **unlocked** (Automatic) gives the impulse the number of
  OpenSkiPro2 (the typed number, otherwise the first number to follow), the number of the
  timing device only when OpenSkiPro2 has none; a device number replaced this way is
  written into the comment of the record. A point **locked** takes the number of the
  timing device. A number changed / removed on the device afterwards is applied when the
  point is locked or the record has no number, otherwise only written into the comment.
* Timing: the context menu of the grids has the lock / unlock of the point where it is
  opened (the intermediates together).

## 0.11.0 – 2026-11-15

* **Timing Off-line** (Runs > *Timing Off-line*, like SkiPro *Chronométrage off-line*): the
  timing window with the *Off-Line Mode* panel. The times are downloaded from the memory
  of the timing device (CP540 `#DL`): choose the timing device (the first by default) and
  its run number (session), then *Download*. Start, intermediates and finish follow the
  channel mapping of the device; the number comes only from the device (channels "use
  keyboard"), the others are assigned by hand like in the Timing. A second download adds
  only the new times (and the numbers / cancellations changed on the device).
  *Delete Existing Records* deletes all the timing information of the run (times, statuses,
  penalties, results, timing device paper). Live impulses are ignored - the Timing (on-line)
  records them. Only one timing window, on-line or off-line.
* API: nothing is published while the Off-line timing is open; everything changed is
  published at once when it is closed, without `competitor` messages.
* CP540 simulator: answers `#DL` with its memory.

## 0.10.0 – 2026-10-29

* API: in run 2+ of a total-of-runs race every intermediate time has `total` - the total
  at the intermediate (the total after the previous runs + the intermediate) with its
  rank, gap and the ranks in the output groups, among those of the run who passed it
  (live data, competitor events, results of a run).
* Start List, Options: *Copy the start list of the previous run* - the start list of the
  previous run as it is (also moved by hand), with or without the competitors who did not
  finish it (DNS, NPS, DNF, DSQ).

## 0.9.3 – 2026-10-29

* API: every intermediate time has `groupRanks` - the rank and the gap of the
  intermediate inside each output group of the competitor (live data, competitor
  events, results of a run), like the run and the total times.

## 0.9.2 – 2026-10-29

* Run Analysis: the rank of a sector is printed in brackets, like the rank of a run.

## 0.9.1 – 2026-10-29

* Run Analysis: the sector fields are named *Sector N time*, *Sector N (rank)* and
  *Sector N time (rank)*.

## 0.9.0 – 2026-10-29

* Output: *Run Analysis* (like SkiPro *Analyse de la manche*) - the run ranking with
  the time and the rank of each sector: start - inter 1 - ... - finish, i.e. one sector
  more than the intermediates of the event. The sectors are computed from the printed
  times (they add up to the run time), ranked among the classified (again inside each
  output group); also in the
  menu of the Timing window.

## 0.8.0 – 2026-10-29

* Free header (General Parameters, *Use a free header*): *Header* opens a rich text
  editor (*Print Heading*, like SkiPro) instead of the jury / technical data - bold,
  italic, underline, alignment, font, also by the right button of the mouse; text pasted
  from Word keeps its format. *Load* from a file (HTML, RTF, text) or another event,
  *Save* to an HTML file. The lists with *Print Header* print it on the first page in
  the place of the jury block.
* SkiPro import: the text of the free header (RTF_HEADER) is imported with its format.

## 0.7.8 – 2026-10-29

* Competitor: *Last name in capitals* (a setting of the event, on by default) - off,
  the last names typed in the form or imported from a file keep their case.
* Competitor: an empty category shows the one it will get ("auto: ..."); when the
  gender or the year changes, a category that came from the old ones is computed again
  (a category typed by hand stays).
* Multi-line fields (General Parameters, the title and notes of the lists, the comment
  of an impulse): Tab goes to the next field instead of typing a tab. *Title* and
  *Discipline* have at most 2 lines.

## 0.7.7 – 2026-10-29

* A bib must be unique: the competitor dialog and *Assign Bibs* no longer save a bib
  that another competitor has (the times, the start list and the results belong to
  the bib, the second competitor got no results). To swap two bibs, give one of them a
  free bib first (e.g. +1000).
* After an import of competitors (file, clipboard, another event) the bibs that several
  competitors have are listed; the FIS XML export lists them among the things to check.

## 0.7.6 – 2026-10-29

* Start List: a run without a start list opens it empty and offers the *Options*
  (order by bib, by points, random draw, from the previous run) instead of filling it
  by itself; *Ok* used to save that list although nobody asked for it.
* Print layouts: a line under the fields shows how the columns fit the table as
  printed - with *Auto adjust* whether all the texts fit or which columns are cut
  ("..."), without it the columns in % of the table width (free space, narrowed).
  *Fit widths* sets the Width of each column from its longest text in the list, the
  rest of the table goes to the widest left aligned column.

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
