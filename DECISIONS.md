# Decisions

Append-only log of significant design decisions.
Each entry: D-NNN, with Decided and Recorded dates (ISO 8601), status, context,
alternatives, decision, consequences, and reversal conditions.
Status vocabulary: Proposed | Accepted | Superseded by D-NNN | Deprecated.

---

### D-001 C++20 with Qt6 on Linux
**Decided:** 2026-10-04
**Recorded:** 2026-10-04
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-04)
**Related:** ARCHITECTURE.md, AV-004

**Context.** Pinax is a single-user desktop catalogue for Linux. The schema was
written toolchain-agnostic so the choice could be made on other grounds.

**Options.**
- **A. C++20 with Qt6.** Chosen. Matches the nearest sibling project, Coeus —
  same toolkit, same desktop target, same local-store shape. Qt's item-model
  framework fits a three-panel list-and-detail layout directly.
- **B. Rust core with a Qt6 GUI.** Stronger guarantees in the import and
  metadata layers. Rejected: the cxx-qt boundary adds friction disproportionate
  to a catalogue of a few hundred rows, and the risky code here is I/O-bound
  rather than memory-unsafe.
- **C. Python with PySide6.** Fastest to a working catalogue, with mature xlsx
  and HTTP libraries. Rejected on packaging — shipping a Python desktop app on
  Linux is heavier than the alternative, and this is a tool to keep.

**Decision.** Option A. C++20, Qt6 Widgets, SQLite via Qt SQL or direct
`sqlite3`.

**Consequences.**
- Qt item models become the interface between the database and the views;
  module boundaries in ARCHITECTURE.md are drawn accordingly.
- Excel export needs a third-party writer (libxlsxwriter) rather than a
  standard-library facility.
- Foreign-key enforcement must be asserted per connection in application code
  — see AV-004.

**Reversal conditions.** Revisit if the metadata layer grows beyond simple HTTP
retrieval into something needing stronger concurrency guarantees, or if Qt6
licensing terms change for personal use.

---

### D-002 SQLite as the only store
**Decided:** 2026-10-04
**Recorded:** 2026-10-04
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-04)
**Related:** F-020, F-021, AV-003

**Context.** The catalogue is a few hundred rows, single user, single machine,
and must survive the application being abandoned.

**Options.**
- **A. SQLite, one file.** Chosen.
- **B. Plain files — JSON or Markdown per book.** Rejected: derived series
  completeness needs joins, and hand-rolling them over a directory of files is
  the thing a database already does.
- **C. A server database.** Rejected outright for a single-user desktop tool.

**Decision.** One SQLite file, schema versioned in `schema_version`.

**Consequences.**
- Series completeness can be a view rather than application logic (D-004).
- The file is the backup, subject to the WAL caveat in AV-003.
- A plain-text dump (F-021) is still required so the format is not the lock-in.

**Reversal conditions.** Revisit if the catalogue ever needs to be read by more
than one machine at once.

---

### D-003 One book record per physical copy
**Decided:** 2026-10-04
**Recorded:** 2026-10-04
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-04)
**Related:** F-004

**Context.** Bibliographic data models usually separate a work from its
editions from the copies held. Pinax catalogues one person's shelf.

**Options.**
- **A. Flatten edition facts onto the book row.** Chosen. Publisher, year,
  binding, ISBN, edition note and condition note live on `book`.
- **B. Separate `work`, `edition` and `copy` tables.** Correct in general, and
  rejected here: the owner holds exactly one copy of each title, so two of the
  three tables would hold one row each per book forever.

**Decision.** Option A.

**Consequences.**
- Edition and condition facts — a 1961 first English edition, a misprinted
  spine — are recorded without ceremony.
- A second copy of the same title cannot be represented without a migration.

**Reversal conditions.** Revisit if a second copy of any title is acquired and
worth recording separately, or if the catalogue is ever extended past one
owner's shelf.

---

### D-004 Series completeness is derived, never stored
**Decided:** 2026-10-04
**Recorded:** 2026-10-04
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-04)
**Related:** F-010, D-006, AV-007

**Context.** The source spreadsheet carried a hand-maintained status column
per series — Complete, Complete to date, Incomplete — plus a free-text list of
missing volumes. Both went stale the moment a book was bought.

**Options.**
- **A. A status column on `series`, updated on write.** Rejected: correct only
  as long as every write path remembers to update it.
- **B. Computed at query time from held versus known entries.** Chosen.

**Decision.** Option B. `v_series_status` and `v_missing_entries` compute
completeness from `series_entry`. There is no status column.

**Consequences.**
- Acquiring a volume changes the reported status with no further action.
- A series with no known entries recorded reports Unknown rather than
  Complete, which is honest but means the entries must be populated for the
  feature to mean anything.
- Any future performance concern would be a reason to materialise the view,
  not to reintroduce a hand-maintained column.

**Reversal conditions.** Revisit if the catalogue grows to a size where the
views are measurably slow — far beyond anything currently foreseen.

---

### D-005 Series position is free text with a separate sort key
**Decided:** 2026-10-04
**Recorded:** 2026-10-04
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-04)
**Related:** F-008, AV-006

**Context.** Real positions in the collection include `Broadcast 6.5`, `1-4`,
`3a`, `novellas`, `collection` and `companion`. None are integers.

**Options.**
- **A. An integer position column.** Rejected: cannot represent any of the above.
- **B. Free text only.** Rejected: sorts `10` before `2`.
- **C. Free text `position` plus numeric `sort_position`.** Chosen.

**Decision.** Option C. `position` is displayed; `sort_position` orders.

**Consequences.**
- A half-numbered entry files between its neighbours; an omnibus spanning 1–4
  takes the position of its first volume.
- The two can drift apart if set carelessly, since nothing enforces agreement.
- Code must never parse a number out of `position` — see AV-006.

**Reversal conditions.** None foreseen. Any scheme replacing this must still
represent every form listed above.

---

### D-006 Unowned volumes are rows, not prose
**Decided:** 2026-10-04
**Recorded:** 2026-10-04
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-04)
**Related:** F-009, F-024, D-004, D-010

**Context.** A series needs to know what it contains, not merely what is held,
or completeness cannot be computed and the missing-volume list has to be
maintained by hand.

**Options.**
- **A. A free-text "still missing" field on `series`.** Rejected: unqueryable,
  and duplicates a fact the entries already imply.
- **B. A `series_entry` row with a null `book_id`.** Chosen.

**Decision.** Option B. An entry with no book attached is a known volume not
owned.

**Consequences.**
- The missing-volume list is a view over unattached entries (D-004).
- Those entries render in the list at their own position rather than in a
  separate report (D-010).
- Acquiring the volume attaches a book to the waiting row, so no duplicate is
  created (F-024).
- Populating entries for 144 series by hand is tedious; this is the main
  argument for the ISFDB bulk import listed as a candidate in FEATURES.md.

**Reversal conditions.** Revisit only if series data proves unobtainable in
bulk and hand-entry is abandoned, in which case completeness becomes a
feature that cannot be supported rather than one modelled differently.

---

### D-007 Authors are normalised
**Decided:** 2026-10-04
**Recorded:** 2026-10-04
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-04)
**Related:** F-002, AV-008

**Context.** The source spreadsheet stored authors as display strings, so
"Terry Pratchett" and "Terry Pratchett & Neil Gaiman" counted as two separate
authors and every per-author total was wrong.

**Options.**
- **A. Keep the display string on the book.** Rejected: the bug above.
- **B. An `author` table with a `book_author` join carrying order and role.**
  Chosen.

**Decision.** Option B. Joint credits are several links. Roles cover author,
editor, translator and illustrator, so edited anthologies are representable.

**Consequences.**
- Counting books per author gives the same answer however the author was
  credited.
- CSV import must split credit strings, and will get some wrong — a book
  credited to a genuine duo with an ampersand in one name is the failure case.

**Reversal conditions.** None foreseen.

---

### D-008 Google Books primary, Open Library fallback
**Decided:** 2026-10-04
**Recorded:** 2026-10-04
**Status:** Superseded by D-019 (2026-10-06): Google Books gives no keyless
quota, so Open Library is primary and Google needs a key.
**Authors:** Shane Hartley (with Claude, 2026-10-04)
**Related:** F-012, F-013, F-014, AV-009, AV-010

**Context.** Synopses, covers and categories must come from somewhere free and
unauthenticated. The Goodreads API has been retired since 2020.

**Options.**
- **A. Google Books only.** Good coverage and consistent category formatting,
  but thinner on older and small-press science fiction.
- **B. Open Library only.** Better cover coverage by ISBN, but descriptions are
  patchy and subjects are inconsistent.
- **C. Google Books primary, Open Library fallback.** Chosen.

**Decision.** Option C, with the provider recorded per field (F-015).

**Consequences.**
- Two client implementations rather than one.
- Per-field provenance makes it possible to tell where a synopsis came from
  and to re-fetch selectively.
- Google Books is quota-limited per IP, so batch enrichment must be resumable
  — see AV-009.

**Reversal conditions.** Revisit if either provider's terms change, or if the
ISFDB import lands and proves a better source for this collection's bibliography.

---

### D-009 Provider categories stored verbatim; no subgenre taxonomy
**Decided:** 2026-10-04
**Recorded:** 2026-10-04
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-04)
**Related:** F-014

**Context.** The wanted granularity was hard SF versus space opera versus
military SF. Free providers do not carry it reliably: Google Books returns
"Fiction / Science Fiction / General" for most of the collection.

**Options.**
- **A. Impose a subgenre taxonomy and classify into it.** Rejected for now: the
  data to populate it does not exist, and the classification is a judgement the
  owner should make rather than inherit.
- **B. A local language model to propose tags.** Rejected: a tag the owner
  chose is worth more than one they have to audit, and the available hardware
  makes a batch run slow for little gain.
- **C. Store what the provider returns, verbatim, with its source.** Chosen.

**Decision.** Option C. Categories are opaque strings. Hand-entered categories
are marked `manual` and are distinguishable from fetched ones.

**Consequences.**
- Genre filtering works at whatever granularity the provider happened to supply.
- Subgenre tagging stays open as a candidate feature, with bulk edit by author
  and series as the intended path.

**Reversal conditions.** Revisit if a source of reliable subgenre data appears,
or once enough hand-tagging has accumulated to be worth formalising.

---

### D-010 Three-panel layout; the detail panel describes the selection
**Decided:** 2026-10-04
**Recorded:** 2026-10-04
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-04)
**Related:** F-016, F-017, F-018, D-006, D-011

**Context.** The catalogue has a filter dimension, a list and a detail record —
the shape every mail client and library tool converges on.

**Options.**
- **A. Filter rail, list, detail panel.** Chosen.
- **B. Rail that switches the application between modes.** Rejected: more
  screens to design and a mode the user has to track.

**Decision.** Option A. The rail filters; the detail panel describes whatever
is selected — a book, a series, or a multi-selection, which becomes a bulk
editor. Volumes recorded as missing (D-006) render in the list at their own
position rather than in a separate report.

**Consequences.**
- One rule governs the right-hand panel in all three cases.
- The missing-volume view needs no screen of its own.
- Lists scoped to a series are slightly noisier, which is the accepted cost.

**Reversal conditions.** Revisit if the inline missing-volume rows prove more
distracting than useful in daily use.

---

### D-011 Editing happens in the detail panel; no modal dialogues
**Decided:** 2026-10-04
**Recorded:** 2026-10-04
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-04)
**Related:** F-005, F-007, D-010

**Context.** Editing could live in dialogues, inline in the list, or in the
detail panel.

**Options.**
- **A. Modal dialogues.** Rejected: breaks the keyboard flow and hides the
  record being edited.
- **B. Inline editing in the list.** Rejected: too little room for synopsis,
  edition and condition fields.
- **C. The detail panel gains an edit state.** Chosen.

**Decision.** Option C. The panel has a view state and an edit state. There are
no modal dialogues anywhere in the application. *Amended by D-027
(2026-10-08): choosing a file uses the system's dialogue.*

**Consequences.**
- Focus never leaves the window, which matters for the single-keystroke read
  toggle and the rating control.
- The bulk editor is the same panel in its edit state for a multi-selection.

**Reversal conditions.** None foreseen.

---

### D-012 Books are added by ISBN with a confirmation step
**Decided:** 2026-10-04
**Recorded:** 2026-10-04
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-04)
**Related:** F-011, F-024, D-006, D-008, AV-010

**Context.** Creating an empty record and enriching it later inverts the useful
order and leaves a window in which the catalogue holds a book nobody has
verified.

**Options.**
- **A. Create, then enrich.** Rejected, per the above.
- **B. Look up by ISBN, confirm, then create.** Chosen.

**Decision.** Option B. Nothing is written until the match is confirmed. The
confirmation checks for a duplicate, proposes a series where one matches, and
where the candidate fills a volume recorded as missing (D-006), attaches to
that entry rather than creating a second record.

**Consequences.**
- The primary add path depends on the metadata layer, so it lands in Phase 3.
  CSV import carries the existing backlog until then.
- Needs no schema change: attaching is an update to `book_id` on an existing
  `series_entry` row.
- A mis-typed ISBN that resolves to a real but wrong book is still possible —
  see AV-010.

**Reversal conditions.** None foreseen.

---

### D-013 No LICENSE while the repository is private
**Decided:** 2026-10-04
**Recorded:** 2026-10-04
**Status:** Superseded by D-032
**Authors:** Shane Hartley (with Claude, 2026-10-04)
**Related:** README.md

**Context.** The development documentation standard lists `LICENSE` as Tier 1
and requires any Tier 1 omission to be recorded here.

**Options.**
- **A. Pick a licence now.** Rejected for the moment: the choice does not
  affect development and is better made against a real intent to publish.
- **B. Omit it while the repository stays private.** Chosen.

**Decision.** Option B. No `LICENSE` file for now; this entry is the audit trail
the standard requires.

**Consequences.**
- The repository must stay private. Without a licence, published code grants
  no rights to anyone.

**Reversal conditions.** Revisit before the first public commit, or if anyone
else is given access to the repository. This entry is superseded at that point
by one recording the licence chosen.

---

### D-014 Webcam barcode capture with Qt Multimedia and ZXing-C++
**Decided:** 2026-10-05
**Recorded:** 2026-10-05
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-05)
**Related:** F-024, F-025, AV-012

**Context.** ISBNs are the bottleneck for synopsis and cover retrieval: none
of the 443 seeded books carries one, and typing thirteen digits per book is
the slowest step in adding anything. A USB webcam is the intended scanner. No
webcam is owned yet, so the decision fixes the approach ahead of the hardware.

**Options.**
- **A. Keyboard-emulating USB barcode scanner.** Needs no code, since it types
  into the ISBN field. Not rejected — it works with F-024 regardless — but a
  dedicated device was not the preference.
- **B. Webcam, Qt Multimedia for capture, ZXing-C++ for decoding.** Chosen.
  Qt Multimedia stays inside the D-001 toolkit. ZXing-C++ is Apache-2.0,
  actively maintained, packaged on the target distribution (`libzxing-dev`),
  reads EAN-13 with the 5-digit add-on separable, and ships Qt integration
  examples.
- **C. Webcam with zbar for decoding.** Rejected: LGPL, less actively
  maintained, and no advantage for a single barcode symbology.
- **D. Phone app sending scans to the desktop.** Rejected for now: needs a
  network listener, which sits uneasily with "no server component" in
  FEATURES.md, Out of scope.

**Decision.** Option B. Frames come from `QCamera` through a `QVideoSink`,
are reduced in size and decoded by ZXing-C++, restricted to EAN-13. A decoded
value goes through the same ISBN validation as typed input (SPEC.md §2) and
the capture rules in SPEC.md §6.

**Consequences.**
- Two new build dependencies: Qt6 Multimedia and ZXing-C++. Both are optional
  at build time; without them the feature is compiled out and typing an ISBN
  is the only path.
- Capture lives in the `io` module, which already owns everything crossing
  the application boundary. It has no network or database access: it emits a
  validated ISBN and nothing more.
- Hardware matters. A webcam with **autofocus** reads a barcode at 10–20 cm; a
  fixed-focus one is usually sharp only beyond about 50 cm and will struggle.
  Buy for autofocus, not resolution.
- Decoding runs on the UI thread at first. If preview stutters, it moves to a
  worker, per ARCHITECTURE.md §4.

**Reversal conditions.** Revisit if the chosen webcam cannot read barcodes
reliably at a usable distance, in which case Option A becomes the scanner and
F-025 is withdrawn; or if ZXing-C++ ceases to be packaged for the target
distribution.

---

### D-015 The `db` module uses the SQLite C API directly, with no Qt
**Decided:** 2026-10-05
**Recorded:** 2026-10-05
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-05)
**Related:** D-001, D-002, AV-004, AV-005

**Context.** D-001 left the database access layer open between Qt SQL and the
SQLite C library. The `db` module is the only one that issues SQL, so the
choice is contained there.

**Options.**
- **A. Qt SQL (`QSqlDatabase`, `QSqlQuery`).** Ships with Qt. Rejected: values
  arrive as `QVariant` and must be unpacked by hand anyway; the driver hides
  SQLite's extended result codes, which is how a constraint violation is told
  apart from anything else; and connections are managed by name through a
  global registry, which makes "every connection has foreign keys on" harder
  to guarantee than a constructor that does it.
- **B. The SQLite C API behind three small RAII wrappers.** Chosen.
  `Connection`, `Statement` and `Transaction`, a few hundred lines in all. The
  schema's views and triggers do the real work, so the wrapper stays thin.

**Decision.** Option B. `pinax_db` links `SQLite::SQLite3` and `pinax_domain`
and has no Qt dependency. Failures are thrown as `DbError` carrying the
extended result code; callers for whom a failure is expected catch it and turn
it into a value (ARCHITECTURE.md §4).

**Consequences.**
- `Connection`'s constructor is the single place foreign keys are enabled, and
  it verifies the setting took (invariant 6). WAL is set there too.
- `PRAGMA` statements left `db/schema.sql`: SQLite refuses to change journal
  mode inside a transaction, and the migration runner applies the schema in
  one transaction so that a failure leaves nothing behind. The schema version
  is unchanged; pragmas are connection state, not schema.
- `db/schema.sql` is compiled into the binary at configure time, so the
  application needs nothing from the source tree at run time.
- The module cannot use Qt's categorised logging. Callers log what `db`
  throws, under `pinax.db`.
- New build dependency: SQLite development headers (`libsqlite3-dev`), 3.31 or
  later.

**Reversal conditions.** Revisit if a second Qt-based consumer of the database
appears that would benefit from Qt SQL's model classes, or if the wrapper grows
past what is reasonable to maintain by hand.

---

### D-016 CSV import is a command-line option for now
**Decided:** 2026-10-05
**Recorded:** 2026-10-05
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-05)
**Related:** F-003, D-011

**Context.** The importer needs a way to be told which file to read. The usual
answer, a File → Import menu item opening a file chooser, is a modal dialogue,
which D-011 rules out everywhere in the application.

**Options.**
- **A. A `QFileDialog` from a menu.** Rejected: modal, contrary to D-011, and
  the first exception would not be the last.
- **B. An inline file chooser in the detail panel.** Consistent with D-011,
  but it is real UI work for an action taken once or twice in the
  catalogue's life, ahead of the panel existing at all.
- **C. `pinax --import file.csv [database]`.** Chosen.

**Decision.** Option C. The import runs before the window opens; failures are
printed as `file:line: message`, the summary goes to standard output and the
status bar.

**Consequences.**
- The seed import, its only near-term use, needs nothing more.
- Re-running is safe by construction (AV-002), so the option can sit in a
  shell history without risk.
- An in-application import, when one is wanted, is option B.

**Reversal conditions.** Revisit when the detail panel exists and an import
path from inside the application is wanted, for example for F-023 round
trips.

---

### D-017 Unmarking a read book takes back one read
**Decided:** 2026-10-05
**Recorded:** 2026-10-05
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-05)
**Related:** F-005, F-006

**Context.** F-005 puts the read state on a single key. `trg_book_finished`
counts every move into `read`, which is right for a finish and wrong for a
slip: R pressed twice by accident would leave a book unread with a read
counted, and pressed three times would record a re-read that never
happened.

**Options.**
- **A. The toggle only ever marks read.** Rejected: F-005 is a toggle, and a
  mistaken mark could then only be undone in the edit form, where the count
  is deliberately not editable.
- **B. Unmarking leaves the count alone.** Rejected: the count drifts upward
  with every slip and nothing shows it happening.
- **C. Unmarking takes back one read, and clears the finish date when none
  are left.** Chosen.

**Decision.** Option C. The toggle on a read book sets it unread and
decrements `times_read` by one, never below nought; at nought,
`date_finished` is cleared. Marking it read again counts and dates it through
the trigger as usual. A genuine re-read still runs read → reading → read and
is untouched by this rule. With several books selected, R marks them all read
unless every one is read already, in which case it unmarks them all; books
already in the target state are not written, so they are not counted again.

**Consequences.**
- A toggle pressed twice leaves the count where it was.
- The finish date of a book unmarked and marked again becomes the day of the
  second marking; the earlier date is not remembered.
- Unmarking a book read twice leaves it unread with one read counted and its
  last finish date kept, which reads as "read once before".

**Reversal conditions.** Revisit if reading history is ever kept as dated
events rather than a counter, at which point an unmark would delete the
latest event instead.

---

### D-018 Known-but-unowned volumes import from their own CSV
**Decided:** 2026-10-05
**Recorded:** 2026-10-05
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-05)
**Related:** F-009, F-010, D-004, D-006, D-016, AV-007

**Context.** Series completeness means nothing until each series knows what
it contains (D-006). The source spreadsheet recorded that by hand for all 144
series — a "Still missing" note and a status — and Phase 2 starts by bringing
it in. The book CSV of SPEC.md §1 has no place for a volume that is not on
the shelf.

**Options.**
- **A. An `owned` column in the book CSV.** Rejected: every other column of a
  book row describes a book; a missing volume has no read state, rating or
  ISBN, and F-023's round trip of the book list would carry rows that are not
  books.
- **B. A second CSV, one row per missing volume or series flag.** Chosen.
- **C. Hand entry through the Phase 2 entry editor only.** Rejected for the
  seed: 287 volumes across 93 series is a day's typing for a fact the
  spreadsheet already records.

**Decision.** Option B. `pinax --import-series series.csv` (SPEC.md §1.6),
beside `--import` (D-016). A row names a series and a volume by position,
title or both, a series' `ongoing` flag, or both. Matching is within the
series on position, else title; a volume already on the shelf at that
position is left alone. Nothing is removed by an import.

For the seed, the owner chose unnamed placeholders where the spreadsheet
gave a count or a gap but no titles ("14 of 41 novels", "Later volumes"):
titled "Unidentified volume n" or "Later volumes — unidentified", noted with
the spreadsheet's words, to be named or removed in the entry editor.

**Consequences.**
- All 144 series' derived statuses match the spreadsheet's hand-kept ones:
  51 complete (4 of them to date) and 93 with gaps.
- Placeholders make counts right before they make names right: Discworld
  reads 27 of 41, though its 14 missing novels are not yet identified.
- A volume removed from the file is not removed from the catalogue; removal is
  the entry editor's.

**Reversal conditions.** Revisit if an ISFDB bulk import (FEATURES.md,
Candidates) lands, which would supply series contents wholesale and could
replace this file for most series.

---

### D-019 Open Library first; Google Books only with a key
**Decided:** 2026-10-06
**Recorded:** 2026-10-06
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-06)
**Related:** D-008, F-012, F-013, F-014, AV-009, AV-010

**Context.** D-008 made Google Books primary on the understanding that it
allowed roughly a thousand unauthenticated requests a day. Recording test
fixtures on 2026-10-06 found otherwise: every request made without an API key
is refused with HTTP 429 and `"quota_limit_value": "0"` for
`defaultPerDayPerProject`. Keyless, Google Books provides nothing. Open
Library answered the same lookups without a key.

**Options.**
- **A. Open Library primary; Google Books as a second opinion when the owner
  supplies a key.** Chosen.
- **B. Keep Google primary and require a key.** Rejected: enrichment would do
  nothing until the owner creates a Google Cloud project, and the first
  experience of Phase 3 would be a failure.
- **C. Open Library only.** Rejected for now: Google's categories are cleaner
  and its descriptions sometimes fuller, and the cost of supporting it behind
  a key is one client already written.

**Decision.** Option A. Open Library answers ISBN lookups (the Books API for
the edition, then the edition and work records for a synopsis) and
title-and-author searches. `GoogleBooksClient` is constructed with a key and
reports itself unavailable without one; it sends nothing keyless. Where the
key is kept — application settings or an environment variable — is settled
when enrichment is wired into the application.

**Consequences.**
- Enrichment works out of the box.
- Open Library subjects are noisier than Google categories; stored verbatim
  all the same (D-009), with their source recorded.
- An ISBN lookup costs up to three requests rather than one, so the daily
  budget matters less than politeness: requests are spaced a second apart
  (D-020).

**Reversal conditions.** Revisit if Open Library's coverage of the owner's
catalogue proves poor in practice, or if Google restores a keyless quota.
This entry supersedes D-008.

---

### D-020 The metadata module: Qt Network, one seam, no live network in tests
**Decided:** 2026-10-06
**Recorded:** 2026-10-06
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-06)
**Related:** D-001, D-019, AV-009, ARCHITECTURE.md §2

**Context.** Phase 3 needs HTTP. D-001 named Qt Network among the Qt modules;
this entry records how the `metadata` module uses it.

**Options.**
- **A. Qt Network behind a `Fetcher` interface, asynchronous on the UI
  thread.** Chosen.
- **B. libcurl.** Rejected: a second HTTP stack beside the Qt one, and
  blocking calls would need a worker thread for no gain at this scale.

**Decision.** Option A. `NetworkFetcher` is the only code that reaches the
network; it identifies itself as `Pinax/<version> (personal library
catalogue)` and sends nothing personal — no owner email or name. Every
request goes through a `RequestQueue` per provider: at least a second between
starts, a pause for HTTP 429 or 503 (honouring `Retry-After`, otherwise a
doubling backoff), retries for other server and network failures, and a
failure handed back only after a fixed number of attempts. Parsing is pure
functions over response bytes. Tests replace the fetcher with recorded
responses (`tests/fixtures/`) and never contact a provider.

**Consequences.**
- `pinax_metadata` links Qt Core and Network, unlike `db` and `io`.
- A test can drive the queue's pauses and retries in milliseconds.
- A recorded response can go stale as a provider changes; the fixtures note
  when each was recorded.

**Reversal conditions.** Revisit if enrichment ever needs more concurrency
than one polite request at a time per provider.

---

### D-021 Where the Google Books key is read from
**Decided:** 2026-10-06
**Recorded:** 2026-10-06
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-06)
**Related:** D-019, D-013

**Context.** D-019 asks Google Books only with an API key. The owner has one
and asked that it never reach GitHub; it was kept in `google-books.key` in
the project root, git-ignored. The running application needs to find it
without the key ever being compiled in, and an installed Pinax will not run
from the project.

**Options.**
- **A. An environment variable, then a file beside the catalogue, then a file
  in the working directory.** Chosen.
- **B. A settings entry (QSettings).** Rejected for now: no settings screen
  exists, and a key in `~/.config` is no safer than one beside the database.
- **C. The desktop keyring (libsecret).** Rejected: a new dependency for a
  key whose worst case is someone else spending a free quota.

**Decision.** Option A. `app::findGoogleBooksKey` reads
`PINAX_GOOGLE_BOOKS_KEY` if set; otherwise the first non-empty
`google-books.key` in the catalogue's folder, then in the folder Pinax was
started from — which covers `build/src/pinax` run from the project, where the
owner's key now lives. Surrounding whitespace is dropped. With no key, Google
is not asked and the panel says Open Library alone. The key travels only in
the request URL to Google; it is never logged, stored in the database, or
written into a fixture.

**Consequences.**
- `.gitignore` carries `/google-books.key` and `*.key`.
- A key beside the catalogue moves with it, as covers do (SPEC.md §4).
- A backup of the data folder includes the key; that is acceptable for a
  personal key and noted here so it is not a surprise.

**Reversal conditions.** Revisit when a settings screen exists, or if the key
ever guards anything that costs money.

---

### D-022 The British Library as a third provider, filling gaps by ISBN
**Decided:** 2026-10-06
**Recorded:** 2026-10-06
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-06)
**Related:** D-009, D-019, D-020, AV-009, AV-010

**Context.** Open Library knew six of eight ISBNs tried from the owner's
shelf but often lacked page counts and gave a reprint's year rather than the
original's; Google, even with a key, knew none (IMP-007). The owner's
library is largely UK editions, which the British Library catalogues as the
legal deposit library. Its published metadata services are Z39.50 for
registered libraries, for non-commercial use; its Alma catalogue also
answers SRU over plain HTTPS with no key
(`bl.alma.exlibrisgroup.com/view/sru/44BL_MAIN`), which is not advertised
on its metadata services page. Jisc Library Hub Discover, which includes
British Library holdings, sits behind a browser challenge and cannot be
used by an application.

**Options.**
- **A. SRU over HTTP, by ISBN, filling the gaps in Open Library's answer.**
  Chosen.
- **B. Z39.50.** Rejected: a registered login meant for libraries, and a new
  dependency (YAZ) for a binary protocol.
- **C. The British Library as a separate candidate.** Rejected by the owner:
  one fetch could not then take Open Library's synopsis with the British
  Library's page count.

**Decision.** Option A. An ISBN lookup asks Open Library and the British
Library at once, each through its own polite queue. Where both answer, Open
Library's candidate stands and the British Library fills only what it lacks
— publisher, page count, publication and first-published years, subtitle —
and adds its subject headings as genres under its own name. Where only the
British Library answers, its record is the candidate. Records whose own ISBN
is not the one asked are dropped, since the index returns related editions.
The British Library supplies no synopsis or cover, so it is never the source
of either; schema version 5 lets `book_genre.source` be `british_library`
and leaves the book's source columns as they were. Not asked for title
searches.

**Consequences.**
- Edition facts for UK books improve markedly: on 2026-10-06 it filled page
  counts for *Firstborn* and *Titan* and original years for four of five.
- Open Library's publisher stays where it has one, so the group name
  ("HarperCollins Publishers") can stand where the British Library names the
  imprint ("Harper Voyager").
- Genre names differ in case between providers ("Science fiction",
  "Science Fiction") and are stored as distinct genres (IMP-009).
- The endpoint is used lightly — one request per ISBN, a second apart — for
  a personal, non-commercial catalogue. Its terms are not published; asking
  metadata@bl.uk would settle them.

**Reversal conditions.** Revisit if the British Library withdraws open SRU
access or asks that it not be used this way; if so, fall back to Option B
with registration, or drop the provider.

---

### D-023 Batch enrichment: resume by status, review held in memory
**Decided:** 2026-10-06
**Recorded:** 2026-10-06
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-06)
**Related:** D-011, D-019, D-022, AV-009, AV-010, SPEC.md §3.4

**Context.** Phase 3 step 5 fetches across the catalogue. A run must
survive interruption and provider trouble (AV-009) and must not write a
title-and-author match unasked (AV-010). None of the owner's 443 books has
an ISBN yet, so nearly every find will need the owner's eye.

**Options.**
- **A. Resume from `metadata_status`; hold the review queue in memory.**
  Chosen.
- **B. A table of pending candidates (schema version 6).** Rejected for now:
  candidates are provider data in flux, a table for them is a schema change
  and a serialisation of `Candidate`, and losing the queue costs only one
  request a book to rebuild.
- **C. Auto-accept the first title-and-author match.** Rejected: AV-010.

**Decision.** Option A. Fetch all looks up every book whose status is
`unmatched`, one at a time through the shared polite queues; matched,
failed and manual books are skipped, which is all resuming needs. An ISBN's
single answer is written at once only if its title agrees with the book's
(`domain::titlesAgree`: case and punctuation aside, the provider may add
only a bracketed or appended part, as in "Titan (NASA Trilogy)"); every
other find waits in the review queue. Nothing found marks the book
`failed`. A provider out of reach stops the run, leaving the book in hand
unmatched. Review walks the queue in the panel: Use this writes as a single
fetch does; Not my book marks the book `failed`, so later runs leave it and
Fetch metadata can still try it; Skip sends it to the back; Stop reviewing
keeps the rest. Cancellation is per channel — the panel's fetch, the batch,
covers — so stopping one never drops the other's answers.

**Consequences.**
- Quitting with matches unreviewed loses only the queue: those books are
  still `unmatched`, and the next run finds them again.
- `failed` covers both "no provider knew it" and "the owner said none of
  these"; the distinction is not recorded.
- A full run over the owner's catalogue is about 440 requests at one a
  second — some eight minutes — plus one or two more per match taken.

**Reversal conditions.** Revisit with option B if review sessions turn out
to span many restarts, or if re-finding becomes slow enough to notice.

---

### D-024 Add by ISBN: the card, held copies, and the catalogue's spellings
**Decided:** 2026-10-07
**Recorded:** 2026-10-07
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-07)
**Related:** D-003, D-006, D-007, D-012, D-022, AV-006, AV-007, AV-010

**Context.** D-012 settled the order — look up, confirm, then create. Building
it against the owner's catalogue raised three questions it did not answer.
None of the 443 seeded books has an ISBN, so the first books scanned will
mostly be ones already held, which a duplicate check by ISBN cannot see.
Providers spell authors their own way ("Iain Banks" for "Iain M. Banks"), so
a candidate's names would create second authors (AV-008's failure by another
route). And a series can be proposed only from what a provider says, which
for UK editions is the British Library's series statement (D-022).

**Options.**
- **A. A card in the panel that recognises held copies and proposes the
  catalogue's spellings and series places, all editable.** Chosen.
- **B. Duplicate check by ISBN only, provider names as given.** Rejected: on
  this catalogue it would duplicate the books scanned first, and split
  authors.

**Decision.** Option A. Before anything is written the card shows the cover,
title, authors, imprint, edition and first-published years, page count and
source. If a book already held has no ISBN, the same title and a shared
author, the card asks whether this is that copy; yes gives it the ISBN and
the candidate's details under the enrichment rules (SPEC.md §3.5), no adds
another copy (D-003). Author names the catalogue already holds replace the
provider's where surname and first name agree — an initial standing for any
first name with its letter — and the field stays editable. Series: a missing
volume whose title agrees and whose series shares an author, or an untitled
gap at the provider's own series and number, is offered for filling with
the series' completeness after; failing that, a tracked series the provider
names is offered at the number it gives, sorted by the importer's rule
(AV-006), but never a series that already has the title. The first proposal
is chosen and can be declined. Declining the candidate falls back to a
search by title and author, nothing preselected (AV-010), keeping the typed
ISBN and taking only what a search may give; failing that, the ordinary form
opens prefilled.

**Consequences.**
- Scanning the shelf fills ISBNs across the backlog, as F-025 intends.
- `app` links `io`, for `deriveSortPosition` alone.
- Name matching is loose by design and only ever proposes; two authors with
  the same surname and first name would be offered as one, and the owner
  corrects the field.
- Adding is three writes — the book, its details, its new series entry —
  not one transaction; a failure after the first is reported with the book
  kept.

**Reversal conditions.** Revisit the held-copy match if it proposes the
wrong book in practice, or once most books carry ISBNs and it matters less.

---

### D-025 libxlsxwriter writes the Excel workbook
**Decided:** 2026-10-07
**Recorded:** 2026-10-07
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-07)
**Related:** D-001, F-022, AV-011

**Context.** F-022 needs an `.xlsx` file that opens without repair prompts.
D-001 anticipated a third-party writer; Qt has none, and the file is a zip
of SpreadsheetML parts with rules of its own.

**Options.**
- **A. libxlsxwriter.** Chosen: a small C library, packaged by the target
  distribution (`libxlsxwriter-dev`, 1.1.5 on Linux Mint 22.3), write-only,
  long used to produce files Excel accepts.
- **B. Pinax's own writer over zlib.** Rejected: some 250 lines of zip and
  XML to keep right, for a format whose failures show up as Excel's repair
  prompt — the very thing the acceptance forbids.

**Decision.** Option A, linked by `pinax_io` alone through pkg-config, so no
other module sees it. The workbook is assembled as plain data
(`domain::Workbook`) by `Catalogue::workbook()` from the views, and
`io::writeWorkbook` only lays it out — bold, frozen, filterable headers,
numbers as numbers — writing a partial file renamed into place on success.

**Consequences.**
- Building Pinax needs `libxlsxwriter-dev` (BUILD.md); it brings zlib.
- The workbook's figures cannot drift from the application's (AV-011): no
  formula or count is computed in the writer.
- Reading a workbook back is not possible with this library; tests read it
  through LibreOffice where installed.

**Reversal conditions.** Revisit if the library leaves the distribution, or
if Pinax ever needs to read spreadsheets, which it cannot.

---

### D-026 A book in several series is a CSV row per series
**Decided:** 2026-10-07
**Recorded:** 2026-10-07
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-07)
**Related:** D-016, F-003, F-008, F-023, AV-002, BUG-005

**Context.** F-023 requires an exported CSV to re-import without loss. The
import format (SPEC.md §1) has one `series` column, and since BUG-005 a book
can be put in several series from the edit form. The importer refused any
second row resolving to a book an earlier row had written — a rule that
catches mistakes in a hand-made file.

**Options.**
- **A. A row per series; a further row for a book may add a series and
  nothing else.** Chosen.
- **B. Several series in one cell, separated somehow.** Rejected: a new
  notation inside the format, with positions and sort numbers to pair up,
  and a separator a series name might contain.
- **C. Export only a book's first series.** Rejected: loss, and silent.

**Decision.** Option A. The exporter writes a row per series, identical but
for `series`, `position` and `sort_position`. The importer accepts a
further row for a book already written by the file only if applying it
would change nothing about the book or its credits and it names a series not
yet given for that book in the file; that row adds the series. Any other
repeat is still refused, by line, as before.

**Consequences.**
- The owner's seed, with one series per book, imports exactly as before.
- A hand-made file can now give a book a second series by repeating its row.
- Two copies of one book without an ISBN cannot round-trip through CSV —
  the importer takes them for one — and the export refuses rather than
  merge them (D-003); the SQL dump keeps both.

**Reversal conditions.** Revisit if the import format ever gains a column
that identifies a book outright, such as a catalogue id.

---

### D-027 Files are chosen in the system dialogue; commands live in a menu bar
**Decided:** 2026-10-08
**Recorded:** 2026-10-08
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-08)
**Related:** D-011, F-020 to F-028

**Context.** Opening, creating, importing and restoring catalogues (F-026 to
F-028) all begin by choosing a file. D-011 forbade modal dialogues anywhere,
so Back up and Export took a typed path with completion — workable for
saving, poor for finding a file to open. The commands also outgrew the
toolbar.

**Options.**
- **A. The system's file dialogue for choosing a file, everything else in
  the panel; a menu bar.** Chosen.
- **B. Typed paths everywhere, toolbar only.** Rejected: a file to open must
  be found, not remembered.

**Decision.** Option A. D-011 is amended to this one exception: choosing a
file, through the system's own open or save dialogue. Every outcome, error
and question still appears in the panel; there are no message boxes. Back
up and Export keep their typed path and gain Choose…. Commands live in File,
Books and Help menus, sharing their actions — and shortcuts — with the
toolbar. Tests supply the choice through `MainWindow::setFileChooser` and
never show a dialogue.

**Consequences.**
- The keyboard flow D-011 protects is broken only while a file is being
  chosen, which the owner has asked for.
- Close Catalogue is Ctrl+W, not Qt's Ctrl+F4.

**Reversal conditions.** None foreseen.

---

### D-028 Pinax remembers the catalogues it has opened
**Decided:** 2026-10-08
**Recorded:** 2026-10-08
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-08)
**Related:** F-026, D-002

**Context.** With more than one catalogue possible, launch must know which
to open, and the owner should not hunt for a file used yesterday.

**Options.**
- **A. Remember the last few in Pinax's settings file; reopen the last.**
  Chosen.
- **B. Always open the default.** Rejected by the owner.

**Decision.** Option A. Qt's settings file (`~/.config/Pinax/Pinax.conf`)
holds `catalogue/recent`, up to eight paths, most recent first;
`RecentCatalogues` keeps it and leaves out files that have gone. At launch
a catalogue named on the command line wins; otherwise the last one open;
otherwise `~/.local/share/pinax/pinax.db`. The command-line tasks
(`--backup`, `--dump` and the rest) follow the same rule.

**Consequences.**
- A scheduled `pinax --backup` with no catalogue named backs up the one last
  open in the window; name it to be sure.
- The settings file holds paths only — no library data, no key.

**Reversal conditions.** None foreseen.

---

### D-029 The owner may say a searched candidate is their edition
**Decided:** 2026-10-08
**Recorded:** 2026-10-08
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-08)
**Related:** F-012, F-015, AV-001, AV-010, SPEC.md §3.5

**Context.** A fetch took publisher and page count only from an ISBN match,
since a title search finds a work whose editions differ. None of the
owner's books had an ISBN, so the panel showed a page count — 576 for
*Cello's Gate* — that was never saved, which the owner took for a bug.
Building the remedy showed a second problem: Open Library's search answers
with a work, whose page count is the median of its editions and whose
publishers are all of theirs. For *Consider Phlebas* the first-listed
publisher is the German Heyne.

**Options.**
- **A. A tick-box, "This is my edition", off by default, that takes the
  edition's publisher and page count; for an Open Library work, from the
  edition behind the cover shown.** Chosen.
- **B. Always take them.** Rejected: another edition's facts, sometimes no
  edition's at all.
- **C. Never, but say so plainly.** Rejected by the owner.

**Decision.** Option A. A searched candidate's publisher and page count are
taken, where empty, only when the owner ticks the box. Open Library's work
figures are marked typical (`Candidate::editionFactsTypical`): the card
shows "about 576 pages" and no publisher, and `planEnrichment` never writes
typical figures. With the box ticked, `Enricher::complete` fetches the
edition named by the search's `cover_edition_key` — the one whose cover the
owner saw — and its own publisher, pages and year replace the typical ones.
Google's results are single editions already. An ISBN is never taken from a
search, ticked or not; ISBNs come from the book itself (D-024).

**Consequences.**
- The edition behind a work's cover may not be the owner's — *Consider
  Phlebas*'s is German, *Bedenke Phlebas*, 762 pages — so the cover, now
  enlarged on hover, is what the owner judges by.
- One more request, to Open Library, when the box is ticked on its result.
- An Open Library work with no cover edition offers nothing to take; its
  typical figures stay unwritten.

**Reversal conditions.** Revisit if Open Library's search comes to name
editions outright.

---

### D-030 Light and dark themes are Pinax's own palettes over Fusion
**Decided:** 2026-10-08
**Recorded:** 2026-10-08
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-08)
**Related:** F-029, D-001, D-028

**Context.** The owner asked for a light and a dark theme. Pinax took the
desktop's style and palette as it found them, though the mock-up (design/)
is dark. Much of the panel muted its text by baking a colour into a widget's
palette when it was built, so a theme changed later would not have reached
it.

**Options.**
- **A. View ▸ Theme: System, Light, Dark. Light and Dark set Qt's Fusion
  style and a palette of Pinax's own; System puts back the desktop's style
  and palette as they were at start-up. Applied at once and remembered in
  the settings of D-028.** Chosen.
- **B. A stylesheet for each theme.** Rejected: stylesheets override the
  palette widget by widget and fight the native styles. Every view would
  need rules, and every new widget too.
- **C. Follow the desktop only.** Rejected: that is what the owner had.

**Decision.** Option A, in `ui/theme.h`. The Dark palette is the mock-up's
near-black browns and parchment text. Light is a warm paper, so the amber
accent sits in both. Muted text is drawn in a palette *role*,
`PlaceholderText`, through `ui::setMuted`, not in a colour copied from the
palette. A change of theme therefore reaches it without anyone setting it
again. The few colours still baked, the progress bars' stylesheets and the
rail's counts, are redone: `SeriesView` on `QEvent::PaletteChange`, and
the rail and panel by the window redrawing them after a choice. Qt tells
existing widgets of a new application palette only once its event loop runs,
so `applyTheme` tells them itself; the saved theme is applied at start-up,
before that.

**Consequences.**
- System is the default, so nothing changes for anyone who never opens the
  menu.
- Light and Dark use Fusion whatever the desktop uses, so they look the same
  on every Linux desktop.
- Anything new that colours text must use `setMuted`, a palette role, or
  redo its colour on `PaletteChange`. A colour copied from the palette once
  is stuck in the theme it was copied in.

**Reversal conditions.** Revisit if Qt's own colour-scheme support (Qt 6.5
and later) reaches the target distribution, which would let System follow a
desktop that changes between light and dark.

---

### D-031 Series volumes come from Wikidata, then Open Library, and are confirmed
**Decided:** 2026-10-09
**Recorded:** 2026-10-09
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-09)
**Related:** F-030, D-018, D-019, AV-009, AV-010, SPEC.md §3.8

**Context.** The owner's series carry 167 volumes known to be missing but
not named: an entry with no title, or a placeholder such as "Unidentified
volume 3" (D-018). The owner asked for their titles to be fetched, to know
what to buy. Open Library and Google Books, the book providers, have no
series lists: Open Library has no series field at all, and Google numbers
only some ebook series.

**Options.**
- **A. Wikidata's query service, which records a book "part of the series"
  (P179) with its number (P1545); Open Library's search for titles carrying
  the series' name when Wikidata does not know the series. The owner ticks
  what is right before anything is written.** Chosen.
- **B. Wikidata only.** Rejected: on 2026-10-09 it knew 8 of the owner's 21
  series with unnamed volumes by exact name. Indie series whose titles carry
  the series' name, such as *Spinward Fringe Broadcast 10*, are found only
  by Open Library.
- **C. Write numbered matches without asking.** Rejected by the owner, and
  against AV-010: Wikidata's numbering may not be the owner's (it numbers
  *New Spring* 0 in The Wheel of Time).
- **D. Scrape a fan wiki or a bookseller's series page.** Rejected: no
  stable interface, and terms that forbid it.

**Decision.** Option A. One SPARQL request: Wikidata's own entity search
finds items named like the series (up to 20), and their books of a listed
kind come back with number, authors and year. The kinds are literary work,
novel, written work, book, novella, short story collection and short story,
listed outright because the subclass path times out beside the search. Of
the series found, the one sharing an author with the owner's series is
taken. Several Wikidata items share a name: "Dune" is both Herbert's six
novels and an item holding only *The Dune Encyclopedia*. A name with the
owner's own prefix ("SW: The New Jedi Order") is asked again as what
follows the colon. When Wikidata finds nothing, Open Library is searched for
the name and first author. Its titles that contain the name are kept,
omnibuses aside, unnumbered. `domain::planSeriesTitles` decides where each
title would go, and the owner confirms in the panel. No new dependency:
Qt Network, as D-020; Wikidata gets its own polite queue (AV-009).

**Consequences.**
- An unnumbered slot ("Unidentified volume 3") named from a numbered volume
  takes that number as its position and sort key. That is the slot's meaning
  made exact, and the owner sees it before using it.
- "Later volumes — unidentified" stands for any number of volumes and is
  never filled this way; `isOpenEndedPlaceholder`, in `placeholder.h`, is
  the one test for it (IMP-005).
- Wikidata is community-edited: a volume may be missing (*Vatta's War* 2)
  or numbered differently. The review is where that is caught.
- A series neither provider lists finds nothing. The owner names its
  volumes by hand, as before.

**Reversal conditions.** Revisit if Open Library adds series data, or if
Wikidata's query service starts to require a key or forbids this use.

---

### D-032 Pinax is released under the MIT licence
**Decided:** 2026-10-10
**Recorded:** 2026-10-10
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, 2026-10-10)
**Related:** D-013, D-001, D-015, D-025, README.md

**Context.** D-013 left out a licence while the repository was private, and
set its reversal at the first public commit. The owner has made the
repository public, so the code needs a licence. Without one it grants no
rights to anyone.

**Options.**
- **A. MIT.** Short and permissive: use, change and redistribution for any
  purpose, keeping the copyright notice. Chosen by the owner.
- **B. GPL-3.0.** Copyleft: modified versions that are distributed stay
  open. Rejected by the owner.
- **C. Apache-2.0.** Permissive, with an explicit patent grant and a duty to
  mark changes. Rejected by the owner: more than a personal project needs.

**Decision.** Option A. `LICENSE` at the root holds the MIT text, copyright
2026 Shane Hartley. The dependencies allow it: Qt 6 under the LGPL v3,
linked dynamically as the distribution ships it (D-001); SQLite, in the
public domain (D-015); libxlsxwriter, under the BSD 2-clause licence
(D-025). Pinax ships none of their code.

**Consequences.**
- Anyone may use, change and redistribute Pinax, keeping the notice.
- The owner's library stays out of the repository as before: `seed/`, `*.db`
  and `*.key` remain git-ignored, and screenshots come from the demo
  catalogue. Making the code public does not make the library public.
- A distributor who bundles Qt with Pinax, rather than relying on the
  system's, takes on the LGPL's conditions for Qt; Pinax's own licence does
  not change that.

**Reversal conditions.** A licence already granted cannot be withdrawn from
copies already taken. A different licence for later versions would need a
new entry.

