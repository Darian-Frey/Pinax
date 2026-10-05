# Changelog

All notable changes to Pinax are recorded here. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versioning follows
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Entries reference stable IDs — F- for features, D- for decisions — so that a
change can be traced to the capability or decision that motivated it.

---

## [Unreleased]

Phase 1 (catalogue core) closed 2026-10-05.

### Added
- Adding and deleting books (F-001): Add a book (Ctrl+N) opens an empty form
  in the panel; Delete in the list or the panel asks in the panel, Keep being
  the default, and deletes in one transaction. A book created as read starts
  at one read (AV-005).
- Credits are editable in the form, in the import notation (F-002); the
  parser moved from `io` to `domain::parseCredits` so import and edit share it.
- A sortable Reads column in the list (F-006).
- Read toggle (F-005, Phase 1 step 6): R in the list marks the selected books
  read, or unread if all of them are read already, saved at once with the
  selection kept. Unmarking takes back the read it counted (D-017).
- Rating control (F-007): 1–9, and 0 for 10, in the list; Backspace or − to
  clear; the detail panel's squares are clickable with a hover preview, the
  current one clearing. Both act on every selected book, in one transaction.
- A key hint in the status bar, as the mock-up's footer has it.
- Detail panel (Phase 1 step 5, D-010, D-011): the selected book in its view
  state — cover placeholder, title, authors, read state and count, rating as
  ten squares, each series with "Book 5 of 10", held against known and the
  missing volumes, synopsis with its source, and the edition facts, with
  "not recorded" where nothing is. Several selected books show a count until
  the bulk editor exists.
- Edit state on F2 or Edit: the book's own fields in a form, checked before
  saving (ISBN check digits, years, ISO dates), problems shown inline, Esc to
  cancel, Ctrl+Enter to save. The read count is shown, not edited (F-006); an
  edited synopsis is marked manual (AV-001).
- `app::Catalogue`, joining the repositories for the window, and the reads it
  needs: `BookRepository::summary`, `SeriesRepository::membershipsForBook`
  over `v_series_status` and `v_missing_entries`.
- CSV import (F-003, Phase 1 step 4): `pinax --import file.csv` (D-016), in a
  new Qt-free `io` module. RFC 4180 reader; idempotent matching on ISBN-13 or
  title and first-billed author, updating only what differs (AV-002);
  `times_read` written explicitly (AV-005); joint credits split into separate
  authors with roles (AV-008, D-007); missing volumes filled rather than
  duplicated (AV-007); sort positions derived per SPEC.md §1.2. One
  transaction per run with a savepoint per row, so bad rows are reported by
  line and skipped.
- `AuthorRepository`, `SeriesRepository`, `Savepoint`, and `BookRepository`
  matching and credit methods. Domain gains `Author`, `Credit`,
  `SeriesEntry`, `CreditRole`, surname-first sort names and ISBN-10/13 check
  digits.
- The seed catalogue converted to `seed/library.csv` (git-ignored): 443 books,
  176 read, 144 series; imports with no failures and re-imports unchanged.
- Book list view (Phase 1, step 3): `BookListView` over `BookListModel` and
  `BookSortProxy` in a new `ui` module, showing read state, title, author,
  series, rating and year, opening sorted by author. Sorting uses the
  database's keys — filing title, author surname, series then
  `sort_position` — with missing values last in both directions (F-016,
  AV-006).
- Schema version 2, the first migration: `v_book_display` gains
  `author_sort`, `series_label`, `series_sort`, `series_sort_position` and
  `date_finished`. Migration steps live in `db/migrations/`, are compiled in,
  and are proven against a frozen version 1 fixture.
- `BookRepository::summaries()`, reading the list from the view.
- `db` module on the SQLite C API (D-015): `Connection` enabling and verifying
  foreign keys on every open (AV-004) and setting WAL; `Statement` and
  `Transaction` wrappers; a migration runner that applies the compiled-in
  schema to an empty file in one transaction and refuses a newer database;
  `BookRepository` with create, find, update, delete and count (F-001).
- `domain` module: `Book`, the read-state, binding, metadata-status and source
  enumerations with their schema strings, and sort-title derivation that
  files 'The Long Earth' as 'Long Earth, The' (F-016).
- The application opens or creates `~/.local/share/pinax/pinax.db`, or a file
  given on the command line, and shows the volume count.
- Tests `test_domain` and `test_db`: migrations, foreign-key enforcement,
  cascades, the re-read counter (F-006, AV-005), rating bounds (F-007),
  duplicate ISBN-13 refusal.
- Project skeleton (Phase 1, step 1): CMake build for C++20 and Qt 6.4+, a
  `pinax_app` library holding the main window shell — a splitter with empty
  rail, list and detail panels per D-010 — and the `pinax` executable. One
  headless Qt Test confirms the window opens with its three panels.
- `BUILD.md`, written on the first successful build.
- Project scaffold against the development documentation standard: `README.md`,
  `FEATURES.md`, `ROADMAP.md`, `CLAUDE.md`, `CHANGELOG.md`.
- `db/schema.sql`, schema version 1. Tables for books, authors, series,
  series entries and genres; credits and genre links carry source attribution;
  derived views `v_book_display`, `v_series_status` and `v_missing_entries`;
  triggers maintaining modification timestamps and the re-read counter.
- Feature register F-001 to F-024 covering catalogue core, reading state,
  series, metadata enrichment, browsing and export.
- F-024, add a book by ISBN with a confirmation step, raised during UI design.
  Attaches to a waiting series entry where one matches, so acquiring a missing
  volume closes the gap rather than creating a parallel record. No schema
  change needed.

### Decided
- Unmarking a read book takes back one read and, at nought, its finish date
  (D-017).
- Import runs from the command line until an in-panel file chooser exists;
  a modal file dialogue would break D-011 (D-016).
- Database access uses the SQLite C API directly, with no Qt in the `db`
  module (D-015).
- Webcam barcode scanning planned as F-025 (Could), Phase 5, blocked on
  hardware. Capture with Qt6 Multimedia, decoding with ZXing-C++, both optional
  at build time (D-014). Capture rules in SPEC.md §6; misread risk as AV-012.
- Three-panel layout: filter rail, list, detail. The detail panel describes
  whatever is selected — a book, a series, or a multi-selection.
- Editing happens in the detail panel rather than in dialogues; the panel has a
  view state and an edit state. No modals anywhere in the application.
- Volumes recorded as missing render in the list at their own position, set in
  italic and marked, rather than in a separate report.

- `ARCHITECTURE.md` — six modules, data flow, eight invariants. Toolchain
  fixed as C++20 with Qt6 (D-001).
- `DECISIONS.md` — D-001 to D-013, all Accepted, covering the toolchain, the
  store, the one-copy-per-book model, derived completeness, free-text
  positions, unowned volumes as rows, normalised authors, the provider pair,
  verbatim categories, the three-panel layout, panel editing, add-by-ISBN, and
  the LICENSE exemption.
- `SPEC.md` — CSV import format with `sort_position` derivation, ISBN-10 and
  ISBN-13 check digits, Google Books and Open Library request shapes and field
  mappings, cover cache layout, and the three export formats.
- `ATTACK_VECTORS.md` — AV-001 to AV-011. Detection reads `not implemented`
  throughout, correctly: there is no code to detect anything in yet.
- `BUGS.md` and `IMPROVEMENTS.md` — empty, created ahead of any code so that
  Maintenance Rule 8 applies from the first commit rather than being adopted
  after the habit of silent fixing has set in.

### Notes
- BUG-003 logged: three editors in the seed were imported as authors named
  "ed. …". IMP-003 suggested: remove authors left with no books.
- IMP-002 suggested: keep an edit in progress when the list selection moves.
- IMP-001 suggested and applied: within an author, books sort by series and
  position, standalones last, instead of interleaving by title.
- BUG-002 found and fixed: `v_book_display` joined credits and series in
  arbitrary order, so joint authors could display out of cover order (F-002).
  Fixed in the schema version 2 view with the owner's approval.
- `PRAGMA` statements moved out of `db/schema.sql` into the connection, so the
  schema can be applied in a single transaction. Schema version unchanged.
- BUG-001 found and fixed: F-001 said deleting a book removes its series
  entries, the schema keeps them as unowned volumes. Settled in favour of the
  schema — a deleted book leaves a missing volume behind — by amending F-001
  and the Phase 1 acceptance. No schema change.
- `LICENSE` is deliberately absent; the omission is recorded as D-013 and the
  repository stays private until it is revisited.
