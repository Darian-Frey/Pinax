# Changelog

All notable changes to Pinax are recorded here. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versioning follows
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Entries reference stable IDs — F- for features, D- for decisions — so that a
change can be traced to the capability or decision that motivated it.

---

## [Unreleased]

### Added
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
- `PRAGMA` statements moved out of `db/schema.sql` into the connection, so the
  schema can be applied in a single transaction. Schema version unchanged.
- BUG-001 logged: F-001 says deleting a book removes its series entries; the
  schema keeps them as unowned volumes. Open, for the author to decide.
- `LICENSE` is deliberately absent; the omission is recorded as D-013 and the
  repository stays private until it is revisited.
