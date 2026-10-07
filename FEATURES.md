# Pinax — Features

Capability list with MoSCoW priorities and acceptance criteria.
IDs are stable and append-only; withdrawn entries keep their ID and gain a status.

---

## Target users

A single owner cataloguing a personal physical library of several hundred
volumes, on a Linux desktop, offline except when enriching metadata.

The design assumes one reader, one copy of each title, and a collection
concentrated in a few genres with heavy series membership. It does not assume
ISBNs are known in advance — most of the catalogue is seeded from a
spreadsheet of titles and authors.

---

## Catalogue core

### F-001 Book records
**Priority:** Must
**Acceptance:**
- A book can be created, edited and deleted with at minimum a title.
- Deleting a book removes its credits and genre links, and leaves no orphan
  rows. Its series entries stay, with no book attached, so each series it
  belonged to now shows that volume as missing (D-006).
- Every record carries creation and modification timestamps without manual input.
**Status:** Complete (2026-10-05). Add a book (Ctrl+N) opens an empty form in the
panel; F2 edits; Delete asks in the panel and deletes in one transaction,
credits and genre links going with the book and its series entries staying
as missing volumes (BUG-001).
**Notes:** `db/schema.sql`, table `book`.

### F-002 Multiple authors with roles
**Priority:** Must
**Acceptance:**
- A book may credit any number of people, each with a role of author, editor,
  translator or illustrator.
- Cover order is preserved, so a joint credit displays in the order printed.
- A person contributing to several books appears exactly once in the author
  table, and counting books per author gives the same total whether that
  author worked alone or jointly.
**Status:** Complete (2026-10-05). Credits are edited as text in the import notation
(SPEC.md §1.1), parsed by one shared `domain::parseCredits`; names resolve to
existing authors, and an author left credited by nothing is removed
(IMP-003). BUG-003, three editors the seed recorded as authors, is fixed.
**Notes:** Flattened author strings in the seed spreadsheet mis-count joint
credits; this is the fix.

### F-003 Import from CSV
**Priority:** Must
**Acceptance:**
- A CSV of title, author, series, position, read status and rating imports
  without manual intervention.
- Rows naming an author or series already present reuse it rather than
  duplicating it.
- Import is idempotent: running the same file twice leaves the catalogue
  unchanged rather than doubled.
- A row that cannot be imported is reported with its line number and does not
  abort the remaining rows.
**Status:** Complete (2026-10-05). `pinax --import` (D-016); the 443-book seed
imports with no failures and re-imports unchanged. Format and rules in
SPEC.md §1.
**Notes:** Seeds the catalogue from the existing 443-row spreadsheet. Format in
SPEC.md §1. See AV-002 (a second run must not double the catalogue) and AV-005
(`times_read` must be written explicitly for already-read books). Author
splitting per D-007.

### F-004 Edition and condition notes
**Priority:** Should
**Acceptance:**
- Publisher, year, binding, ISBN, edition note and condition note are
  recordable per book.
- Free-text edition and condition notes are preserved verbatim and never
  overwritten by a metadata fetch.
**Status:** Complete — every field is recordable and editable in the detail
panel, and a fetch never writes the edition or condition note, nor a
publisher, year or page count already recorded (AV-001; 2026-10-06,
`enrichmentNeverTouchesWhatTheOwnerWrote`, `enrichingNeverOverwritesTheOwnersWork`).
**Notes:** Carries facts a provider will not know — printing, jacket state,
misprints, acquisition.

---

## Reading state

### F-005 Read/unread toggle
**Priority:** Must
**Acceptance:**
- A book's state changes between unread and read with one keystroke from the
  list view, without opening a dialogue or losing the current selection.
- The change is persisted immediately.
**Status:** Complete (2026-10-05). R in the list toggles the selection and saves at
once; the selection stays. Unmarking takes back the read it counted (D-017).

### F-006 Re-read counter
**Priority:** Should
**Acceptance:**
- Each transition into the read state increments a count and stamps the date.
- The count is visible in the list view and sortable.
- Importing an already-read book sets the count explicitly rather than
  relying on a state transition.
**Status:** Complete (2026-10-05). The trigger counts and dates each move into read;
imports and new books write the count explicitly (AV-005); the list shows it
in a sortable Reads column and the panel as "read twice".
**Notes:** See trigger `trg_book_finished`; re-reads run read → reading → read.

### F-007 Rating out of ten
**Priority:** Must
**Acceptance:**
- A book takes an integer rating from 1 to 10, or none.
- Unrated is distinguishable from rated zero, and sorting separates the two.
- A value outside the range is rejected rather than clamped.
**Status:** Complete (2026-10-05). Keys 1–9 and 0 (for 10) in the list, Backspace
or − to clear; the detail panel's squares are clickable, the current one
clearing. Unrated sorts after every rating; out-of-range values are
rejected, not clamped.

---

## Series

### F-008 Series membership
**Priority:** Must
**Acceptance:**
- A book may belong to more than one series.
- Position is stored as printed, including non-numeric forms such as
  `Broadcast 6.5`, `1-4` and `3a`.
- Ordering is correct for half-numbered entries and for omnibuses spanning
  several positions.
**Status:** Complete (2026-10-06). Positions are stored as printed with a sort number
entered beside them, never derived outside the importer (AV-006); a series
lists in sort order; entries are added, edited and removed on the series'
page; a book may sit in several series.
**Notes:** D-005. `position` is free text; `sort_position` is the numeric sort
key. Never parse one from the other at display time — see AV-006. Derivation
rules in SPEC.md §1.2.

### F-009 Known-but-unowned entries
**Priority:** Must
**Acceptance:**
- A series entry can be recorded with no book attached, representing a volume
  known to exist but absent from the shelf.
- Attaching a book to that entry later does not create a duplicate.
**Status:** Complete (2026-10-06). Known volumes come in through `--import-series`
(D-018) or are added on the series' page, and show in place, italic and "not
owned". Mark as owned attaches a book — new, or already catalogued — to the
waiting entry in one transaction, never adding a second (AV-007).
**Notes:** D-006. Attaching is an update to `book_id` on the existing row; see
AV-007 for the failure this avoids.

### F-010 Derived series completeness
**Priority:** Must
**Acceptance:**
- Completeness is computed from held versus known entries at query time and
  is never stored as a status column.
- A series still being written reports as complete to date rather than
  complete when every published volume is held.
- A series with no known entries recorded reports as unknown, not complete.
- The missing-volume list is ordered so that series needing one book appear
  before series needing several.
**Status:** Complete (2026-10-06). Derived in `v_series_status` and
`v_missing_entries`, never stored; ongoing series report complete to date,
series with nothing recorded report unknown. A series' page and the panel
show held against known and what is missing; the rail's NEEDS ATTENTION lists
every missing volume, fewest needed first, or only the series one volume
short. All 144 seeded statuses match the spreadsheet. See IMP-006 on
placeholder-only gaps.
**Notes:** D-004. Views `v_series_status`, `v_missing_entries`. Do not add a
stored status column — that is the staleness the views exist to prevent.

---

## Metadata enrichment

### F-011 ISBN entry
**Priority:** Must
**Acceptance:**
- ISBN-13 and ISBN-10 are recordable and validated by check digit.
- A duplicate ISBN-13 is refused.
**Status:** Complete — both forms are validated by check digit in the edit
form and in Add by ISBN (an ISBN-10 is looked up as its ISBN-13); a
duplicate ISBN-13 is refused by the schema, and Add by ISBN reports one held
in either form (2026-10-07).
**Notes:** The field itself. F-024 is the workflow built on it.

### F-012 Synopsis retrieval
**Priority:** Must
**Acceptance:**
- A synopsis is fetched by ISBN where one is known, and by title and author
  otherwise.
- The provider that supplied it is recorded alongside it.
- A failed lookup marks the book as unmatched and leaves prior content intact.
- A synopsis edited by hand is marked manual and survives subsequent fetches.
**Status:** In progress — Fetch metadata in the panel looks one book up, by
ISBN (an ISBN-10 as its ISBN-13) or else by title and first author, and
offers the candidates; the one chosen writes its synopsis with its source. A
lookup that finds nothing records `failed` (SPEC.md §3.4) — "not matched"
in the sense of the criterion above — and changes nothing else. A manual
synopsis survives. Fetch all runs the same lookup over every unmatched book,
taking an ISBN's single agreeing answer and queueing the rest for review
(D-023).
**Notes:** D-019 for the provider pair — Open Library first, Google Books only
with a key, superseding D-008 — and D-022 for the British Library, which
fills edition facts by ISBN but never supplies a synopsis; SPEC.md §3 for the
request shapes. See
AV-001 (never overwrite a manual field), AV-009 (quota) and AV-010 (a
title-and-author match is never auto-accepted).

### F-013 Cover retrieval
**Priority:** Must
**Acceptance:**
- A cover image is downloaded once and cached on disk; the database stores a
  path, not image data.
- A book with no cover found displays a placeholder rather than failing.
- Re-running enrichment does not re-download a cover already cached.
**Status:** In progress — the cover cache downloads once, refuses anything
but a real image, keeps covers beside the database and removes them with
their book; the panel shows a cached cover or the placeholder. A candidate
chosen in Fetch metadata brings its cover, unless the book's cover was set by
hand. Fetch all brings covers with every match it takes or the owner
confirms; a cover already cached is not fetched again. When there are
candidates to choose from, each shows its cover as a thumbnail, so editions
can be told apart before one is chosen (2026-10-07).
**Notes:** From Open Library by cover id; Google as a second source with a key
(D-019). Cache layout in SPEC.md §4.

### F-014 Genre from provider categories
**Priority:** Must
**Acceptance:**
- Categories are stored verbatim as the provider returns them, with no
  taxonomy imposed.
- A book may carry several categories.
- The source of each category is recorded, so a hand-entered category is
  distinguishable from a fetched one.
- The catalogue can be filtered and grouped by category.
**Status:** In progress — a chosen candidate's categories are stored verbatim
in `genre`/`book_genre` with their source, beside any the owner added, which
keep theirs; the panel lists a book's genres. Filtering and grouping by
genre wait for Phase 4 (F-017, F-018). The British Library's subject
headings arrive as genres under its own name (D-022, schema version 5).
**Notes:** Subgenre classification at the level of hard SF versus space opera
is explicitly out of scope at this version — see below.

### F-015 Provenance on retrieved fields
**Priority:** Should
**Acceptance:**
- Every fetched field records which provider supplied it and when.
- A field entered by hand is never silently replaced by a fetched value.
**Status:** In progress — synopsis, cover and each genre record their
provider; the book records `metadata_status` and `metadata_fetched_at`.
Publisher, year and page count carry no source column, so a fetch fills
them only when empty and never replaces one (SPEC.md §3.5). A per-field
source for those would need a schema change, not proposed.

### F-024 Add a book by ISBN
**Priority:** Must
**Acceptance:**
- Entering an ISBN retrieves a candidate and shows its title, author, imprint,
  edition year, first-published year, page count and cover **before** anything
  is written to the catalogue.
- Nothing is created until the match is confirmed. Declining falls back to a
  title and author search, and failing that to manual entry.
- An ISBN already held is reported as such and does not create a second record.
- Where the candidate belongs to a series already tracked, the series and
  position are proposed rather than typed.
- Where the candidate matches a known-but-unowned entry (F-009), that entry is
  reused rather than a duplicate created, and the resulting series
  completeness is stated before confirming.
- The book is shelved as read or unread at the point of adding.
**Status:** Complete (2026-10-07) — Add by ISBN in the toolbar (Ctrl+I) and
the panel's card; D-024. Beyond the criteria: a copy already held without
an ISBN — the whole seeded backlog — is recognised by title and author and
offered the ISBN instead of a second record, and the provider's author
spelling is replaced by the catalogue's own where they plainly agree.
**Notes:** The primary path for anything acquired from now on; F-003 covers the
existing backlog. Needs no schema change — attaching a book to a waiting
`series_entry` is an update to `book_id` on the row that already exists.

### F-025 Barcode scanning by webcam
**Priority:** Could
**Acceptance:**
- With a USB webcam attached, the add-by-ISBN panel (F-024) shows a live
  preview, and holding a book's barcode to the camera fills the ISBN field and
  starts the same lookup as typing it. Nothing is written until the match is
  confirmed, exactly as for a typed ISBN.
- With a book selected, the detail panel can scan a barcode onto that existing
  record, so ISBNs can be filled across the seeded backlog.
- A read is accepted only when it is a valid ISBN-13 by check digit, carries a
  `978` or `979` prefix, and is seen identically in consecutive frames. A
  5-digit price add-on is discarded.
- A scanned ISBN already held by another book is reported, not assigned.
- After a confirmed add, the panel stays open and ready for the next book, so a
  stack can be scanned without touching the keyboard.
- With no camera attached the application behaves exactly as without this
  feature; typing an ISBN is always available.
**Status:** Not started — waiting on hardware
**Notes:** D-014 for the libraries, SPEC.md §6 for the capture rules, AV-012
for misreads. Books printed before barcodes were common — roughly pre-1975,
*Playing with Infinity* among them — have none and are entered by hand.

---

## Browsing

### F-016 Sort
**Priority:** Must
**Acceptance:**
- The catalogue sorts by title, author, series position, rating, year,
  date finished and times read.
- Title sorting ignores a leading article.
- Null values sort predictably and consistently in one direction.
**Status:** In progress — the list view sorts by title, author, series
position, rating, year and read state by column header, with missing values
last in both directions; within an author, by series then position
(IMP-001). Date finished and times read are in
`v_book_display` but have no column or sort control yet.

### F-017 Filter
**Priority:** Must
**Acceptance:**
- The catalogue filters by read state, rating range, genre, series and author.
- Filters combine, and the active filter set is visible and clearable in one action.
**Status:** In progress — the rail filters by read state and by series. Rating,
genre and author filters, and combining filters, are not built.
**Notes:** An author filter and its counts must use role 'author' credits
in `book_author`, not `v_book_display.authors`, which shows editors where a
book has no author (IMP-004).

### F-018 Group
**Priority:** Should
**Acceptance:**
- The catalogue groups by series, author or genre, with group headers showing
  a count.
- Within a series group, books order by series position.
**Status:** Not started

### F-019 Search
**Priority:** Must
**Acceptance:**
- A single search field matches against title, author and series.
- Results appear without an explicit submit action.
**Status:** Not started

---

## Export

### F-020 Database backup
**Priority:** Must
**Acceptance:**
- A consistent copy of the database is written to a chosen path while the
  application is running, without closing or locking out the live database.
- The copy opens as a valid database and reports the same book count.
**Status:** Not started
**Notes:** D-002. `VACUUM INTO` is the intended mechanism; a plain file copy of
a WAL-mode database mid-write is not safe — see AV-003.

### F-021 Portable dump
**Priority:** Must
**Acceptance:**
- The catalogue exports as plain-text SQL that recreates it on an empty
  database.
- The dump is readable and diff-able, so it can be version-controlled.
**Status:** Not started
**Notes:** Guards against the format itself becoming the lock-in.

### F-022 Excel export
**Priority:** Must
**Acceptance:**
- The catalogue exports as `.xlsx` with separate sheets for books, series
  status and authors.
- Column headers are present and the sheets open without repair prompts.
- Series status and missing volumes appear as computed values, matching the
  application's own views.
**Status:** Not started

### F-023 CSV export
**Priority:** Should
**Acceptance:**
- The current view — filters and sort applied — exports as CSV.
- The output re-imports under F-003 without loss.
**Status:** Not started

---

## Out of scope

- **Ebook management.** No reading, conversion, device sync or file handling.
  This catalogues physical objects.
- **Subgenre taxonomy.** Classification at the level of hard SF, space opera
  or military SF is not attempted. Free providers do not carry it reliably,
  and it is a judgement the owner should make rather than inherit. Deferred,
  not refused — see Candidates.
- **Lending.** No borrower tracking.
- **Valuation.** No price history or condition grading. Condition is a free-text
  note under F-004 and nothing computes against it.
- **Multi-user and sync.** Single local user, single machine.
- **Recommendations.** Nothing suggests what to buy or read next.

---

## Candidate features

Uncommitted. Recorded so they are not re-discovered later.

- **ISFDB bulk import.** The Internet Speculative Fiction Database publishes
  weekly MySQL dumps under a Creative Commons licence, carrying series and
  series-position data for science fiction specifically. Importing it would
  populate F-009 wholesale rather than by hand, and would resolve series
  membership offline. The largest single win available, and the heaviest.
- **Barcode scanning.** *Promoted to F-025 (webcam) on 2026-10-05; phone
  capture remains a candidate.* Webcam or phone capture feeding the same confirmation
  step as F-024, so a stack of new books goes in without typing. Also the way
  to fill ISBNs across the existing backlog, which is the bottleneck for F-012
  and F-013.
- **Full-text search over synopses.** SQLite FTS5. Not justified at a few
  hundred rows, where a substring match is instant.
- **Subgenre tagging.** A free-tag table plus bulk edit by author or series,
  on the observation that several hundred books collapse into roughly a
  hundred author and series clusters.
- **Duplicate detection.** Fuzzy title and author matching to catch a second
  copy bought by accident.
- **Reading statistics.** Books finished per year, rating distribution,
  re-read frequency.
