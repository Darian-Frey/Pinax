# Pinax — Roadmap

Phased development plan. Phases are append-only: mark complete with an ISO
date rather than deleting. Feature IDs refer to `FEATURES.md`.

---

## Phase 0 — Scaffold
**Goal:** Fix the data model, the design decisions and the documentation
standard before any code exists.
**Status:** Complete (2026-10-04)
**Features delivered:** —
**Deliverables:**
- [x] `db/schema.sql` — tables, views, triggers, indexes
- [x] Tier 1: `README.md`, `FEATURES.md`, `ROADMAP.md`, `CLAUDE.md`, `CHANGELOG.md`
- [x] `DECISIONS.md` — D-001 to D-013, all Accepted
- [x] `ARCHITECTURE.md` — six modules, data flow, eight invariants
- [x] `SPEC.md` — CSV format, ISBN validation, provider contracts, export layouts
- [x] `ATTACK_VECTORS.md` — AV-001 to AV-011, detection defined for each
- [x] `BUGS.md`, `IMPROVEMENTS.md` — empty, present so Maintenance Rule 8
      applies from the first commit
- [x] `LICENSE` — resolved as a recorded exemption (D-013), not an omission;
      the repository stays private until it is revisited
- [ ] `BUILD.md` — deliberately deferred to Phase 1, per the standard's
      creation order: written when the first build succeeds
**Acceptance:** Schema applies to an empty database and the derived views
return correct results for a representative sample of the real collection.
Every F-, D- and AV- reference across the documents resolves to a defined
entry, with no gaps in any ID sequence.

---

## Phase 1 — Catalogue core
**Goal:** A working local catalogue holding the existing collection, with
reading state and ratings.
**Status:** Complete (2026-10-05). F-001, F-002, F-003, F-005, F-006 and
F-007 are Complete. F-004 is recordable and editable; its second criterion,
that a metadata fetch never overwrites the notes, is carried to Phase 3,
where the fetch it constrains is built (AV-001).
**Features delivered:** F-001, F-002, F-003, F-004, F-005, F-006, F-007
**Deliverables:**
- [x] Project skeleton: CMake, Qt6 Widgets, a window that opens (D-001) —
      2026-10-05
- [x] Seed data converted from the catalogue spreadsheet to the CSV format in
      SPEC.md §1 — a prerequisite for the importer, not a product of it —
      2026-10-05, `seed/library.csv` (git-ignored)
- [x] `db` module: connection asserting `PRAGMA foreign_keys = ON` (AV-004),
      migration runner keyed to `schema_version`, repositories returning
      domain types — connection, migrations, `BookRepository`,
      `AuthorRepository` and `SeriesRepository` done 2026-10-05 (D-015);
      the genre repository moves to Phase 3, where genres first arrive
- [x] Book list view over `v_book_display`, sortable — 2026-10-05; needed
      schema version 2 for the sort keys
- [x] Detail panel: view state, then edit state (D-011) — 2026-10-05; edits a
      book's own fields; authors and series are not edited there yet
- [x] CSV importer, idempotent (AV-002), writing `times_read` explicitly
      (AV-005), reporting per-row failures without aborting the run —
      2026-10-05, `pinax --import` (D-016)
- [x] Read/unread keystroke binding — 2026-10-05, R (D-017)
- [x] Rating control — 2026-10-05, keys 1–9 and 0, clickable squares
- [x] `BUILD.md`, written once the build succeeds — 2026-10-05
**Acceptance:** The 443-book seed imports in one pass; read state and ratings
survive a restart; re-running the import changes no row count; a book imported
as read reports `times_read >= 1`; deleting a book leaves no orphan credits or
genre links, and leaves its series entries as missing volumes.

---

## Phase 2 — Series
**Goal:** Series membership and completeness, including volumes not owned.
**Status:** Complete (2026-10-06). F-008, F-009 and F-010 are Complete. The
owner's 144 series carry 287 known volumes not owned, and every derived
status matches the spreadsheet's hand-kept one. IMP-006, on placeholder-only
gaps counting as one volume short, is open with the owner and does not hold
the phase.
**Features delivered:** F-008, F-009, F-010
**Deliverables:**
- [x] Known-but-unowned volumes and ongoing flags loaded from the seed —
      2026-10-05, `--import-series` (D-018); 144 of 144 statuses match the
      spreadsheet
- [x] Series list with held, known and status columns backed by `v_series_status`
      — 2026-10-06, the rail's SERIES section; status in each entry's tooltip
- [x] Series detail showing owned and missing entries in position order —
      2026-10-06, a series' own page in the middle panel and its description
      in the detail panel
- [x] Entry editor accepting non-numeric positions — 2026-10-06; add, edit,
      remove, and Mark as owned with a new or existing book (AV-007)
- [x] Missing-volumes report backed by `v_missing_entries` — 2026-10-06, the
      rail's NEEDS ATTENTION: one volume short, and every missing volume,
      fewest needed first (schema version 4 adds `entry_id`)
**Acceptance:** A series with every published volume held reports complete;
one short of a volume reports incomplete and names it; an ongoing series with
everything published reports complete to date.

---

## Phase 3 — Metadata enrichment
**Goal:** Synopses, covers and genre pulled from public providers.
**Status:** In progress
**Features delivered:** F-011, F-012, F-013, F-014, F-015, F-024
**Deliverables:**
- [ ] `GenreRepository`, carried from Phase 1
- [ ] F-004's remaining criterion: a fetch never overwrites edition or
      condition notes, carried from Phase 1 (AV-001)
- [x] HTTP client with rate limiting and retry — 2026-10-06, `RequestQueue`
      over Qt Network (D-020)
- [x] Provider lookups by ISBN and by title/author — 2026-10-06, Open Library
      first and Google Books with a key (D-019, superseding D-008's order)
- [ ] Covers from Open Library, Google as a second source
- [ ] On-disk cover cache keyed by book
- [ ] Batch enrichment with progress, cancellation and resumption
- [ ] Per-field provenance recorded on write
- [ ] Add-by-ISBN flow: lookup, confirmation card, duplicate and series checks,
      and attachment to a waiting series entry
**Acceptance:** A batch run over the whole catalogue completes inside the
provider's daily quota, is resumable after interruption, leaves hand-entered
fields untouched, and reports a per-book matched/unmatched outcome. Adding a
book by the ISBN of a volume recorded as missing attaches it to that entry and
moves the series to complete, without creating a second record.

---

## Phase 4 — Browsing and export
**Goal:** Get data in and out, and find things in it.
**Status:** Not started
**Features delivered:** F-016, F-017, F-018, F-019, F-020, F-021, F-022, F-023
**Deliverables:**
- [ ] Sort, filter and group controls over the list view
- [ ] Search field
- [ ] `VACUUM INTO` backup
- [ ] Plain-text SQL dump
- [ ] `.xlsx` writer: books, series status, authors
- [ ] CSV export of the current view
**Acceptance:** An exported workbook matches the application's own counts for
books, read/unread split and series completeness. A dump restores onto an
empty database and reproduces those same counts.

---

## Phase 5 — Barcode scanning
**Goal:** Scan ISBNs with a USB webcam, for new books and the seeded backlog.
**Status:** Not started — blocked on hardware (no webcam owned yet; buy one
with autofocus, per D-014)
**Features delivered:** F-025
**Deliverables:**
- [ ] Qt6 Multimedia and ZXing-C++ found by CMake as optional dependencies;
      the feature compiles out cleanly without them
- [ ] Capture component in `io`: camera frames → EAN-13 decode → ISBN,
      applying the rules in SPEC.md §6
- [ ] Live preview in the add-by-ISBN panel, feeding the F-024 lookup
- [ ] Scan-to-existing-book from the detail panel, for backfilling ISBNs
- [ ] Tests for AV-012: non-ISBN EAN-13, price add-on, disagreeing frames
- [ ] `BUILD.md` updated with the optional dependencies
**Acceptance:** A paperback held 10–20 cm from the camera produces its
ISBN-13 within two seconds; a shop price sticker and a price add-on never
produce one; a stack of ten new books can be added without the keyboard; and
the application builds and runs unchanged with no camera present.

---

## Later — uncommitted

Not a phase. Candidate features from `FEATURES.md` promoted here only when
committed to.

- ISFDB bulk import, which would populate F-009 across the whole catalogue
  rather than by hand
- Barcode scanning by phone (webcam scanning is committed as Phase 5)
- Subgenre tagging with bulk edit by author and series
