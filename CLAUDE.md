# CLAUDE.md — Pinax

Handoff document for AI development sessions. This is current state, not
history; rewrite it when the state changes rather than appending to it.

---

## 1. Project summary

Pinax is a Linux desktop catalogue for a personal physical library of several
hundred books. It tracks reading state, ratings out of ten, series membership
including volumes not owned, and pulls synopses, cover art and genre categories
from public book metadata providers. Data lives in a single local SQLite
database and exports to SQL, CSV and Excel.

---

## 2. Current state

**Phases 1 and 2 closed (2026-10-05, 2026-10-06).** The application opens and upgrades its database, imports books (`--import`) and series contents (`--import-series`), lists and sorts, edits books and credits in the panel (F2), adds (Ctrl+N), deletes (Delete), toggles read (R) and rates (1–9, 0). The rail filters by read state and series; a series has its own page with the volumes not owned in place, an entry editor and Mark as owned; NEEDS ATTENTION is the shopping list. F-001–F-010 Complete. Phase 3 under way: Fetch metadata in the panel looks one book up, offers candidates, and writes the chosen one under SPEC.md §3.5; Fetch all does it for the catalogue, with Review matches for what needs confirming; Add by ISBN adds a book, or gives a held copy its ISBN, after a confirmation card.

| Path | State |
|---|---|
| `db/schema.sql`, `db/migrations/` | Version 5. `schema.sql` is the latest full schema; `002_book_display_sort_keys.sql`, `003_book_display_editors.sql`, `004_missing_entries_entry_id.sql` and `005_genre_source_british_library.sql` (rebuilds `book_genre`, D-022) carry older files forward. Each step's `CREATE` statements must stay byte-identical to `schema.sql`'s — `migratingVersion1MatchesFreshSchema` fails otherwise. |
| `src/domain/` | Value types (`Book`, `BookSummary`, `BookDetail`, `BookEdit`, `BookFilter`, `Author`, `Credit`, `SeriesEntry`, `SeriesMembership`, `SeriesStatus`), enums with schema strings, `makeSortTitle`, `makeSortName`, ISBN check digits, `isbn10To13` and `isbn13To10`, `knownAuthor`/`shareAnAuthor` (loose name matching, proposals only), `SeriesProposal`, `titlesAgree`, `parseCredits`/`formatCredits`, `isPlaceholderTitle`, `Candidate`, and `planEnrichment` — the one statement of what a fetch may write (SPEC.md §3.5, AV-001). No Qt, no SQL. |
| `src/db/` | SQLite C API, no Qt (D-015). `Connection` (FK on + verified, WAL), `Statement` (named binds), `Transaction` (RAII), `Savepoint`, `migrate()` with schema and migrations compiled in, and `BookRepository`, `AuthorRepository`, `SeriesRepository`, `GenreRepository` (an existing link keeps its source). Errors throw `DbError` with the extended result code; `isConstraintViolation()` tells the data's fault from the database's. |
| `src/metadata/` | Qt Network (D-020). `Fetcher`/`NetworkFetcher` (the one network seam), `RequestQueue` (spacing, pause on 429/503, retries), `CoverCache` (covers beside the database), `OpenLibraryClient`, `GoogleBooksClient` (key only, D-019) and `BritishLibraryClient` (SRU by ISBN, MARC 21, D-022) with pure `openlibrary::`/`googlebooks::` parsers. Used by `app::Enricher`. |
| `src/io/` | Qt-free. `parseCsv` (RFC 4180), `CsvImporter` (SPEC.md §1) and `SeriesImporter` (§1.6), sharing `import_support.h`: one transaction, savepoint per row, failures by line, `deriveSortPosition` (the only code that parses `position`). |
| `src/ui/` | `BookListModel`, `BookSortProxy` (view keys; missing values last; author then series), `BookListView` (opens sorted by author; emits `selectionChangedTo`). `RailView` (LIBRARY, SERIES and NEEDS ATTENTION sections; emits `BookFilter`). `MissingPage` (the shopping list over `MissingModel`) is the third view in the middle stack. `SeriesPage` (heading, show-missing toggle, `SeriesTable` over `SeriesEntryModel`) replaces the book list in the middle stack while a series is chosen. `SeriesView` is the panel's description of a series, with a card for a selected missing volume. `EntryEditor` and `AttachView` are the panel's forms for an entry and for Mark as owned. `list_keys.h` holds the single-key actions both lists share. `BookListView::showOnly` narrows to a set of ids. `DetailPanel` stacks Empty / Viewing (`BookView`) / Editing (`BookEditor`) / Several / ConfirmingDelete (generic `askToConfirm`) / ViewingSeries (`SeriesView`) / EditingEntry (`EntryEditor`) / Attaching (`AttachView`) / Fetching (`CandidateView`: the lookup, then candidates; busy, and nothing redraws over it) / Adding (`AddByIsbnView`: ISBN, card, search and hand-entry fall-backs) / BackingUp (`BackupView`) / Exporting (`ExportView`, a format list that F-022 and F-023 will extend). `BookView` lists genres. `RatingBar`, `style.h` (accent, muted, section headings). Links `domain`, not `db`. |
| `src/app/`, `src/main.cpp` | `Catalogue` (connection + repositories: `summaries`, `detail`, `save(Book)`, `save(BookEdit)` which creates at id 0 and resolves credits, `remove`, `toggleRead`, `setRating`). Also `countWithReadStatus`, `seriesStatuses` (filed as titles), `bookIds(BookFilter)`, `seriesRows`, `seriesDetail`, `entry`, `saveEntry`, `removeEntry`, `attach`, `seriesCredits`, `nextSortPosition`; `save(BookEdit, attachTo)` for Mark as owned; `missingVolumes`, `libraryTotals`; for Add by ISBN `bookWithIsbn`, `creditsFor`, `booksLike`, `seriesProposals`, `addBook`, `giveIsbn`; `dataDirectory`, `setCover` (never over a manual cover), `enrich` (a chosen candidate, one transaction, returns the cover URL), `markLookupFailed`. `Enricher` asks the providers (ISBN then title, Open Library then Google with a key; one queue per host), completes a searched candidate's synopsis, fetches covers, and `cancel`s; it writes nothing. `findGoogleBooksKey` (D-021). `BatchEnricher` runs Fetch all over unmatched books (D-023): takes an ISBN's single title-agreeing answer, queues the rest in memory for review, marks misses failed, stops on unreachable providers. Toolbar: Add a book, Add by ISBN (Ctrl+I), Fetch all metadata, Review matches (n); a progress bar in the status bar. `MainWindow`: splitter of `rail`, `list`, `detail`; rail choice → Catalogue ids → list; selection → panel; panel save → Catalogue → row and rail refreshed. `main` opens `~/.local/share/pinax/pinax.db` or argv[1], runs `--import` and `--import-series`, or `--backup` / `--dump` / `--xlsx` / `--csv <file>` and quits without a window (F-020 to F-023), finds the Google key, builds the `Enricher` over a `NetworkFetcher`, shows the count. |
| `tests/` | Qt Test, headless under ctest: `test_main_window`, `test_domain`, `test_db`, `test_book_list`, `test_rail`, `test_series_page`, `test_entry_editor`, `test_import` and `test_series_import` (their seed tests skip without `seed/`), `test_metadata` (recorded responses only; see `tests/fixtures/README.md`), `test_detail_panel`, `test_catalogue`, `test_enricher` (providers, the panel flow and the batch run end to end, over `fake_fetcher.h`, whose replies die with it). Add new ones with `pinax_add_test`. `fixtures/schema_v1.sql` is frozen. |
| `README.md` | Complete. |
| `FEATURES.md` | Complete. F-001 to F-025. F-001 to F-011 and F-016 to F-024 Complete; F-012, F-013, F-014, F-015 In progress; the rest Not started. |
| `ROADMAP.md` | Complete. Phases 0–2 done; Phases 3 and 4 have every deliverable ticked, closing each the owner's call; Phase 5 (webcam scanning) not started, waiting on hardware. |
| `ARCHITECTURE.md` | Complete. Six modules, eight invariants. |
| `DECISIONS.md` | Complete. D-001 to D-026; D-008 superseded by D-019, the rest Accepted. |
| `SPEC.md` | Complete. CSV format, ISBN validation, provider contracts, what a fetch writes (§3.5), cover cache, export layouts. |
| `ATTACK_VECTORS.md` | Complete. AV-001 to AV-013. Detection implemented for AV-001, AV-002, AV-003, AV-004, AV-005, AV-008, AV-011; partly for AV-006, AV-007, AV-010, AV-012, AV-013; the rest `not implemented`. |
| `BUGS.md` | No open bugs. BUG-001 to BUG-005 fixed. |
| `IMPROVEMENTS.md` | IMP-007 suggested (free-text fallback for Google, whose qualified queries return nothing); IMP-008 suggested (leave Open Library's library-service subjects such as "Accessible book" out of genres); IMP-009 suggested (genres differing only in case are one); IMP-010 suggested (a finish date for books read before Pinax). IMP-001 to IMP-005 applied; IMP-006 deferred. |
| `CHANGELOG.md` | Complete. Unreleased section only. |
| `BUILD.md` | Complete. Written 2026-10-05 on the first successful build. |
| `LICENSE` | **Absent, deliberately.** Exempted by D-013 while the repository is private. |

The owner's catalogue began as a 443-row spreadsheet in `seed/` (176 read,
267 unread, 144 series), with a Markdown rendering alongside; it has been
converted and imported (see §3). `seed/` is git-ignored and must stay so: the
repository has a GitHub remote and the owner's library is not to be
published.

`src/` holds one directory per module (ARCHITECTURE.md §2); all have code. Each module becomes its own static library as it gains
code.
`design/` holds the UI mock-up with PNG captures of its four screens.

---

## 3. Active task

**Phases 3 and 4 have every deliverable done** (Phase 4 on 2026-10-07:
sort, filter, group, search, backup, and SQL, Excel and CSV export).
Closing each is the owner's call, after trying Fetch all and the exports on
the real catalogue. Phase 5 (webcam scanning) waits on a camera. The
Phase 3 record below stays until the phase is closed.

**Phase 3 — Metadata enrichment** (F-011 to F-015, F-024). Phases 1 and 2 are
closed; their history is in ROADMAP.md and CHANGELOG.md.

Suggested order:

1. ~~**The `metadata` module and its first dependency.**~~ Done 2026-10-06
   (D-020), with step 2's clients. Qt Network for HTTP
   (D-001 already names it). A client with a rate limiter and retry that
   pauses on HTTP 429 rather than failing (AV-009), asynchronous on the UI
   thread as ARCHITECTURE.md §4 plans. Tests use recorded JSON responses,
   never the live providers.
2. ~~**Google Books by ISBN, then by title and author** (SPEC.md §3.1), mapped
   to a `domain::Candidate`.~~ Done 2026-10-06, with the providers swapped:
   Google Books gives a keyless quota of 0, so Open Library is primary and
   Google needs a key (D-019, SPEC.md §3). A title-and-author match is never accepted
   without confirmation (AV-010) — and none of the 443 seeded books has an
   ISBN, so this path is the one the backlog takes.
3. ~~**Open Library fallback** for covers and older titles (§3.2), and the
   **cover cache** beside the database (§4).~~ Done 2026-10-06: `CoverCache`
   and `Catalogue::setCover`.
4. ~~**Fetch metadata for one book**, provenance on write (F-015), the
   carried F-004 / AV-001 rule and `GenreRepository`.~~ Done 2026-10-06:
   `domain::planEnrichment` (SPEC.md §3.5), `Catalogue::enrich`,
   `app::Enricher`, the panel's Fetching state; the key as D-021. A real
   fetch of *Surface Detail* into a catalogue copy found it by search and
   wrote synopsis, year, five genres and the cover. The owner's catalogue
   has not been fetched into. The British Library was added the same day
   (D-022): asked with Open Library for an ISBN, it fills publisher, pages
   and original years, and its headings become genres (schema version 5).
5. ~~**Batch enrichment**: progress, cancel, resume after interruption or
   quota (AV-009), and a queue of title-and-author candidates for the owner
   to confirm or reject.~~ Done 2026-10-06 (D-023): `BatchEnricher`, the
   toolbar's Fetch all metadata and Review matches. A live 45-second run on
   a catalogue copy looked up 45 books, queued 40 and found 5 nowhere;
   the owner's catalogue has not had a full run. BUG-004 (a focused list
   preselected a search result) found and fixed on the way.
6. ~~**Add by ISBN** (F-024, D-012): lookup, a confirmation card in the panel,
   duplicate and series checks, attaching to a waiting volume (AV-007).~~
   Done 2026-10-07 (D-024): `AddByIsbnView`, Ctrl+I. Held copies without an
   ISBN are recognised and given it — the way the backlog gains ISBNs. Live
   on a catalogue copy: *Consider Phlebas* (Orbit 2023) fills The Culture to
   10 of 10; *Titan* is recognised as held. The webcam scanner (Phase 5)
   will feed this.

Every Phase 3 deliverable is ticked; closing the phase is the owner's call.

Open with the owner: IMP-007, IMP-008, IMP-009, IMP-010; whether to ask metadata@bl.uk
about the open SRU endpoint (D-022). IMP-006 is deferred.

The seed: `seed/library.csv` and `seed/series.csv`, made by
`seed/convert_catalogue.py` from the spreadsheet (all git-ignored). The converter fixes two credits the ` & `
split would get wrong (`Arkady & Boris Strugatsky`, `Wong, Bukalov &
Slavin`) and turns a leading `ed. ` into an `(editor)` credit (BUG-003).
Re-importing the current seed is safe; re-importing a file older than a credit
correction made in the application is not (AV-013). The owner's catalogue is
`~/.local/share/pinax/pinax.db`; back it up before any change made outside
the application, as was done for BUG-003 — now `build/src/pinax --backup
<file>`, which checks the copy — and never by copying the file (AV-003):

```sh
build/src/pinax --import seed/library.csv
```

---

## 4. Architectural invariants

The canonical list is ARCHITECTURE.md §3. The three most easily broken:

- **Series completeness is computed, never stored.** Do not add a status column
  to `series`; `v_series_status` derives it (D-004).
- **`position` is free text, `sort_position` is the sort key.** Never parse a
  number out of `position` (D-005, AV-006).
- **A field whose source is `manual` is never overwritten by enrichment**
  (AV-001).

---

## 5. Build and test commands

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
build/src/pinax
```

Verified with Qt 6.4.2, GCC 13.3, CMake 3.28 on Linux Mint 22.3; details in
`BUILD.md`. Tests run with `QT_QPA_PLATFORM=offscreen`. Builds use
`-Wall -Wextra -Wpedantic` and are currently warning-free; keep them so.

The schema on its own (it holds no PRAGMAs; the connection sets them):

```sh
sqlite3 pinax.db < db/schema.sql
sqlite3 pinax.db "SELECT version, applied_at FROM schema_version;"
```

Toolchain is C++20 with Qt6 Widgets, the SQLite C API and libxlsxwriter
(D-001, D-015, D-025; `libxlsxwriter-dev`, linked by `pinax_io` alone). Qt
6.4 is the floor because that is what the target distribution ships; Qt 6.4's
`QCOMPARE` cannot compare `std::optional` with a plain value, so tests use
`QVERIFY(a == b)` there. Each dependency added is worth a DECISIONS entry.
`test_catalogue` reads an exported workbook back through LibreOffice when
`soffice` is installed, and skips that part otherwise.

---

## 6. Conventions

- Documentation follows the scaffold standard at
  `github.com/Darian-Frey/project-scaffold`. ID sequences are append-only;
  withdrawn entries keep their ID and gain a status.
- **British English** throughout — documentation, comments and user-facing strings.
- ISO 8601 dates everywhere.
- SQL: keywords uppercase, identifiers `snake_case`, tables singular (`book`,
  not `books`), views `v_`, indexes `idx_`, triggers `trg_`.
- C++: `snake_case` for files, `PascalCase` for types, `camelCase` for
  functions and members, trailing underscore on private data members. One class
  per header where practical.
- Commit messages are multi-paragraph, reference F-/D-/AV- IDs, and carry
  exactly one Subtle Chaos anomaly each — one small unexplained irregularity
  in otherwise ordinary professional prose, never placed at the centre of the
  sentence and never explained. Development-flavoured anomalies preferred,
  and from 2026-10-05 **ominous, and aimed at the reader**: at the owner's
  request the anomaly should make whoever reads the message question what
  they just read — a figure that cannot be right, a sentence that knows it is
  being read, a sequence that ran in the wrong order. Still exactly one, still
  one clause, still never explained, and the technical content around it stays
  accurate. The anomaly is quoted back to the owner after every commit and
  push.
  (Definition as given in Aether's CLAUDE.md; the fuller Subtle Chaos spec is
  not in this repository. The first two commits predate this and carry none.)
- Documentation changes travel in the same commit as the code that invalidates
  them (Maintenance Rule 7).
- **Log, don't act.** A bug found or an improvement noticed while working on
  something else goes into `BUGS.md` or `IMPROVEMENTS.md` and stops there. The
  decision to fix or apply is the author's (Maintenance Rule 8). This is the
  rule most easily broken by an AI partner.

---

## 7. Known pitfalls

`ATTACK_VECTORS.md` is the canonical list — AV-001 to AV-013 cover enrichment
overwriting manual fields, double import, unsafe WAL backup, unenforced foreign
keys, the `times_read` trap, numeric position parsing, duplicate series
entries, joint-credit counting, provider quota, wrong-edition matches, export
drift, barcode misreads, and re-importing a file older than a credit
correction (AV-013).

Not vectors, but worth knowing:

- **A re-read is read → reading → read.** The counter is driven by the state
  transition, not edited by hand, so any re-read control must perform both
  updates. The R toggle is not a re-read: unmarking takes back the read it
  counted (D-017), so `Catalogue::toggleRead` writes `times_read` on the way
  down and leaves the trigger to count on the way up.
- **`BookEdit::series` is nullopt except from the edit form** (BUG-005).
  The form always sends its rows, so a save from it rewrites the book's
  series: a row dropped is a series left. Every other path (Add by ISBN,
  Mark as owned, imports) leaves it nullopt and the book's series alone.
- **The edit form shows `times_read`; it does not set it.** The count moves
  only by read-state transitions through `trg_book_finished` (F-006). A form
  that wrote it would race the trigger.
- **The list is disabled while the panel is busy** (editing, or confirming a
  delete; IMP-002). Tests that drive the list must finish or cancel the edit
  first, and anything new that changes the selection should respect
  `DetailPanel::isBusy()`.
- **Authors are removed when nothing credits them** (IMP-003), unless they
  have notes. Anything that changes credits outside `Catalogue::save` or the
  importer must call `AuthorRepository::removeUncredited()` too.
- **The panel's busy states are eight**: Editing, EditingEntry, Attaching,
  Fetching, Adding, BackingUp, Exporting and ConfirmingDelete. `isEditing()`
  (all but ConfirmingDelete) is what nothing may redraw over; `isBusy()`
  (all eight) is what locks the
  list, the rail and the toolbar's actions. Fetching serves both a single
  fetch and the review of a batch's finds.
- **Enricher cancellation is per channel** (Interactive, Batch, Covers,
  Previews). Stopping the batch must not drop the panel's fetch, nor the
  reverse; covers are never cancelled, so their callbacks guard their owner
  with a `QPointer`. Previews — candidate thumbnails, the Add by ISBN card's
  cover — are also withdrawn from the cover queue (`cancelPreviews`, a
  `RequestQueue` tag) whenever their choice ends; anything new that shows
  candidates should do the same.
- **Focus is not a choice.** A list given focus makes its first row
  current; Use this follows the *selection* (BUG-004). Anything new that
  offers candidates must not enable acceptance from the current row.
- **A new book for a missing volume** is saved with `attachTo`; the window
  keeps the entry id in `pendingAttach_` between "Add it as a new book" and
  Save, and clears it on any dismissal.
- **The middle panel is a stack** of three: the book list (under its
  `FilterBar`, in `listPage_`), a series' page and the shopping list. `showingSeries()` and `showingMissing()` say which is
  live; selection, refresh and focus go through `refreshPanel()` and
  `showSeriesPage()` so both views stay in step. `refreshPanel()` never
  redraws over an open form, but does redraw over a pending delete question —
  that is how Keep returns.
- **The book list's model may hold header rows** (F-018). `view.model()` is
  the `BookGroupProxy`: a row may be a group heading that maps to no book,
  and under Group by genre one book can occupy several rows. Go through
  `selectedBooks()`, `selectBook()` and the proxy's `isHeader()`, never
  `model()->index(row, …)` as if every row were a book.
- **Search text is the list's, not SQL's** (F-019). `BookQuery::text` is
  stripped before `idsMatching`; `BookSortProxy` matches the words against
  `searchTexts()`, refreshed in `refreshRail()` but applied only when the
  search next changes, so the list holds still. The bar compares the field
  trimmed when writing a query back, or a space just typed is lost.
- **The list's filters live in `MainWindow::query_`** (F-017). The rail's
  read states set `query_.readStatus` and keep the rest; All books clears
  it; anything that sends the owner back to every book (`chooseFilter({})`)
  therefore clears the filters too. `applyQuery()` narrows the list and
  shows the read state in the rail silently (`RailView::showFilter`), so it
  never re-emits. Like the rail, filters apply when chosen: the list holds
  still while books are rated or marked read.
- **`v_book_display.authors` is not an author list.** Where a book has no
  author it shows the editors, "(ed.)" and all (IMP-004). Count or filter
  authors from role 'author' rows in `book_author`.
- **A deleted book's id can come back.** SQLite reuses the highest rowid
  once that row is gone, so anything kept outside the database under a book
  id — covers today — must be deleted with the book (`Catalogue::remove`).
- **Tests never touch the network.** Provider behaviour is pinned by recorded
  responses in `tests/fixtures/` (their origins in that folder's README);
  record new ones by hand with `curl`, keeping titles already public in the
  repository, and never anything from `seed/`.
- **The owner has a Google Books API key. It must never reach the
  repository** — not in code, settings committed, fixtures or messages. It
  lives in `google-books.key` at the project root: one line, mode 600,
  git-ignored (`/google-books.key` and `*.key`). Read it from there when a
  key is needed; never copy it elsewhere in the project. How the application
  finds it at run time is settled when enrichment is wired in (D-019). Run
  `git grep -n AIza` before any commit.
- **Google Books without a key answers 429, always.** Do not mistake it for a
  rate limit to wait out: `GoogleBooksClient` sends nothing keyless (D-019).
- **Placeholder volumes are ordinary entries.** "Unidentified volume n" and
  "Later volumes — unidentified" are titles, nothing more; no column marks
  them (D-018). `domain::isPlaceholderTitle` is the only code that reads the
  convention — use it, never a string test of your own (IMP-005). Matching on
  re-import relies on those titles staying put until the owner renames them.
- **An edited synopsis becomes `manual`.** `BookEditor` sets
  `synopsis_source = 'manual'` when the text changes, so enrichment must skip
  it (AV-001). Keep that link when touching either side.
- **A fetch writes only through `planEnrichment`.** Publisher, year and page
  count have no source column, so they are filled only when empty, and
  publisher and pages only from an ISBN match. No fetch writes an ISBN,
  title or credit. Change the rules there and in SPEC.md §3.5 together.
- **Never commit the Google key.** It lives in `google-books.key`
  (git-ignored); grep for its prefix before every commit. Tests use the
  string `test-key`.
- **Series positions are not unique within a series.** An omnibus may share a
  position with its constituent volumes. The unique index covers series plus
  book, not series plus position.
- **`group_concat` order is not set by an `ORDER BY` beside it.** Join from
  an ordered inner subquery instead (BUG-002). The `ORDER BY` inside the
  aggregate needs SQLite 3.44; the floor is 3.31.
- **Schema changes are two edits.** Update `db/schema.sql` (the latest full
  schema, with a `schema_version` row per version) *and* add
  `db/migrations/NNN_*.sql`, then bump `latestSchemaVersion`. The migration
  test compares `sqlite_master` byte for byte, comments inside `CREATE`
  statements included.
- **Cross-references between documents are aspirational.** No `check_xrefs`
  tooling exists in this repository, so a reference from a DECISIONS entry to
  an AV entry may not have a matching reference back. Treat any cross-reference
  as a hint, not a guarantee, until that tooling exists.

---

## 8. Out of scope

Do not implement these without a DECISIONS entry recorded first. Reasoning is
in `FEATURES.md` under Out of scope.

- Ebook reading, conversion or device sync
- Subgenre classification beyond what providers return verbatim (D-009)
- Lending or borrower tracking
- Valuation, pricing or condition grading
- Multi-user access, sync or any server component
- Purchase or reading recommendations
