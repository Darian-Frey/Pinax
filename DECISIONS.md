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
**Status:** Accepted
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
no modal dialogues anywhere in the application.

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
**Status:** Accepted
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
