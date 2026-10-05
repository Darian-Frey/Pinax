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

**Phase 1 built: open, import (`--import`), list and sort, view and edit in the panel (F2) including credits, add (Ctrl+N), delete (Delete, confirmed in the panel), toggle read (R), rate (1–9, 0). F-001–F-003 and F-005–F-007 Complete; F-004's no-overwrite rule waits on enrichment. Phase 1 closed 2026-10-05.**

| Path | State |
|---|---|
| `db/schema.sql`, `db/migrations/` | Version 2. `schema.sql` is the latest full schema; `002_book_display_sort_keys.sql` carries a version 1 file forward. Its `CREATE VIEW` must stay byte-identical to `schema.sql`'s — `migratingVersion1MatchesFreshSchema` fails otherwise. |
| `src/domain/` | `Book`, enums with schema strings, `makeSortTitle`. No Qt, no SQL. |
| `src/db/` | SQLite C API, no Qt (D-015). `Connection` (FK on + verified, WAL), `Statement` (named binds), `Transaction` (RAII), `migrate()` with schema compiled in from `db/schema.sql`, `BookRepository`. Errors throw `DbError` with the extended result code. |
| `src/io/` | Qt-free. `parseCsv` (RFC 4180), `CsvImporter` (SPEC.md §1; one transaction, savepoint per row, reports `line: message`), `deriveSortPosition` (the only code that parses `position`). |
| `src/ui/` | `BookListModel`, `BookSortProxy` (view keys; missing values last; author then series), `BookListView` (opens sorted by author; emits `selectionChangedTo`). `DetailPanel` stacks Empty / Viewing (`BookView`) / Editing (`BookEditor`) / Several. `RatingBar`, `style.h` (accent, muted, section headings). Links `domain`, not `db`. |
| `src/app/`, `src/main.cpp` | `Catalogue` (connection + repositories: `summaries`, `detail`, `save(Book)`, `save(BookEdit)` which creates at id 0 and resolves credits, `remove`, `toggleRead`, `setRating`). Toolbar: Add a book. `MainWindow`: splitter of `rail` (empty), `list`, `detail`; selection → panel, panel save → Catalogue → row refreshed. `main` opens `~/.local/share/pinax/pinax.db` or argv[1], runs `--import`, shows the count. |
| `tests/` | Qt Test, headless under ctest: `test_main_window`, `test_domain`, `test_db`, `test_book_list`, `test_import` (its seed test skips without `seed/library.csv`), `test_detail_panel`, `test_catalogue`. Add new ones with `pinax_add_test`. `fixtures/schema_v1.sql` is frozen. |
| `README.md` | Complete. |
| `FEATURES.md` | Complete. F-001 to F-025. F-001, F-002, F-003, F-005, F-006, F-007 Complete; F-004, F-011, F-016 In progress; the rest Not started. |
| `ROADMAP.md` | Complete. Phases 0 and 1 done; Phases 2–5 not started; Phase 5 (webcam scanning) waits on hardware. |
| `ARCHITECTURE.md` | Complete. Six modules, eight invariants. |
| `DECISIONS.md` | Complete. D-001 to D-017, all Accepted. |
| `SPEC.md` | Complete. CSV format, ISBN validation, provider contracts, cover cache, export layouts. |
| `ATTACK_VECTORS.md` | Complete. AV-001 to AV-013. Detection implemented for AV-002, AV-004, AV-005, AV-008; partly for AV-006, AV-007, AV-013; the rest `not implemented`. |
| `BUGS.md` | No open bugs. BUG-001, BUG-002 and BUG-003 fixed. |
| `IMPROVEMENTS.md` | IMP-004 suggested (show editors in the list when there is no author); owner to decide. IMP-001, IMP-002, IMP-003 applied. |
| `CHANGELOG.md` | Complete. Unreleased section only. |
| `BUILD.md` | Complete. Written 2026-10-05 on the first successful build. |
| `LICENSE` | **Absent, deliberately.** Exempted by D-013 while the repository is private. |

The catalogue itself is a 443-row spreadsheet in `seed/` (176 read, 267
unread, 144 series), with a Markdown rendering alongside. `seed/` is
git-ignored and must stay so: the repository has a GitHub remote and the
owner's library is not to be published. The spreadsheet is the intended seed
for F-003 and has not yet been converted to the CSV format in SPEC.md §1; the
converted CSV belongs in `seed/` too.

`src/` holds one directory per module (ARCHITECTURE.md §2); `domain/`, `db/`,
`ui/` and `app/` have code. Each module becomes its own static library as it gains
code.
`design/` holds the UI mock-up with PNG captures of its four screens.

---

## 3. Active task

**Phase 2 — Series** (F-008 to F-010). Phase 1 closed 2026-10-05; its history
is in ROADMAP.md and CHANGELOG.md.

Suggested order:

1. **Missing volumes from the seed.** The spreadsheet's "Still missing"
   column is free text ("Consider Phlebas (1)", "Books 1-5", "Later volumes",
   "… unwritten") and its "Series status" column marks ongoing series
   ("Complete to date"). Turn what is specific into `series_entry` rows with no
   book, flag ongoing series, and report what is too vague to convert for the
   owner to settle. Needs an import format first — a SPEC.md section and
   probably a DECISIONS entry. Until this lands every series reports
   complete, because nothing is known to be missing.
2. Series in the rail with held/known counts (`v_series_status`), as the
   mock-up's SERIES · 144 section.
3. Selecting a series lists its entries in position order with missing ones
   inline, in italic and marked (D-010, mock-up screen 2), and the panel
   describes the series.
4. Entry editor: add, edit and remove entries, non-numeric positions, "Mark
   as owned" attaching a book (AV-007).
5. Missing-volumes view from `v_missing_entries`, fewest-needed first.

Open with the owner: IMP-004.

The seed: `seed/library.csv`, made by `seed/convert_catalogue.py` from the
spreadsheet (both git-ignored). The converter fixes two credits the ` & `
split would get wrong (`Arkady & Boris Strugatsky`, `Wong, Bukalov &
Slavin`) and turns a leading `ed. ` into an `(editor)` credit (BUG-003).
Re-importing the current seed is safe; re-importing a file older than a credit
correction made in the application is not (AV-013). The owner's catalogue is
`~/.local/share/pinax/pinax.db`; back it up with `VACUUM INTO` before any
change made outside the application, as was done for BUG-003:

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

Toolchain is C++20 with Qt6 Widgets and the SQLite C API (D-001, D-015). Qt
6.4 is the floor because that is what the target distribution ships; Qt 6.4's
`QCOMPARE` cannot compare `std::optional` with a plain value, so tests use
`QVERIFY(a == b)` there. Excel export needs libxlsxwriter. Each dependency
added is worth a DECISIONS entry.

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
- **An edited synopsis becomes `manual`.** `BookEditor` sets
  `synopsis_source = 'manual'` when the text changes, so enrichment must skip
  it (AV-001). Keep that link when touching either side.
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
