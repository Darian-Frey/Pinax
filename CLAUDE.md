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

**Phase 1 steps 1–2 done: the application builds, opens or creates its database, and shows an empty three-panel window with the volume count. No list, import or editing yet.**

| Path | State |
|---|---|
| `db/schema.sql` | Complete. Version 1. Applies cleanly; views verified against sample data drawn from the real collection. |
| `src/domain/` | `Book`, enums with schema strings, `makeSortTitle`. No Qt, no SQL. |
| `src/db/` | SQLite C API, no Qt (D-015). `Connection` (FK on + verified, WAL), `Statement` (named binds), `Transaction` (RAII), `migrate()` with schema compiled in from `db/schema.sql`, `BookRepository`. Errors throw `DbError` with the extended result code. |
| `src/app/`, `src/main.cpp` | `MainWindow`: `QSplitter` of three empty panels `rail`, `list`, `detail`. `main` opens `~/.local/share/pinax/pinax.db` or argv[1], migrates, shows the count. |
| `tests/` | Qt Test, headless under ctest: `test_main_window`, `test_domain`, `test_db`. Add new ones with `pinax_add_test`. |
| `README.md` | Complete. |
| `FEATURES.md` | Complete. F-001 to F-025. F-001 In progress; the rest Not started. |
| `ROADMAP.md` | Complete. Phase 0 done; Phase 1 in progress; Phases 2–5 not started; Phase 5 (webcam scanning) waits on hardware. |
| `ARCHITECTURE.md` | Complete. Six modules, eight invariants. |
| `DECISIONS.md` | Complete. D-001 to D-015, all Accepted. |
| `SPEC.md` | Complete. CSV format, ISBN validation, provider contracts, cover cache, export layouts. |
| `ATTACK_VECTORS.md` | Complete. AV-001 to AV-012. AV-004 detection implemented, AV-005 partly; the rest `not implemented`. |
| `BUGS.md` | BUG-001 open: F-001 says delete removes series entries, schema keeps them as unowned (SET NULL). Author to decide. |
| `IMPROVEMENTS.md` | Empty, by design. Same reason. |
| `CHANGELOG.md` | Complete. Unreleased section only. |
| `BUILD.md` | Complete. Written 2026-10-05 on the first successful build. |
| `LICENSE` | **Absent, deliberately.** Exempted by D-013 while the repository is private. |

The catalogue itself is a 443-row spreadsheet in `seed/` (176 read, 267
unread, 144 series), with a Markdown rendering alongside. `seed/` is
git-ignored and must stay so: the repository has a GitHub remote and the
owner's library is not to be published. The spreadsheet is the intended seed
for F-003 and has not yet been converted to the CSV format in SPEC.md §1; the
converted CSV belongs in `seed/` too.

`src/` holds one directory per module (ARCHITECTURE.md §2); `domain/`, `db/`
and `app/` have code. Each module becomes its own static library as it gains
code.
`design/` holds the UI mock-up with PNG captures of its four screens.

---

## 3. Active task

**Phase 1 — Catalogue core** (F-001 to F-007). Nothing blocks it.

Suggested order:

1. ~~Project skeleton: CMake, Qt6 Widgets, a window that opens.~~ Done 2026-10-05.
2. ~~`db` module: connection with `PRAGMA foreign_keys = ON` asserted (AV-004),
   migration runner keyed to `schema_version`, `BookRepository`.~~ Done
   2026-10-05. Author, series and genre repositories come with step 4.
3. List view over `v_book_display`, sortable.
4. CSV importer per SPEC.md §1 — idempotent (AV-002), `times_read` written
   explicitly (AV-005).
5. Detail panel in its view state, then its edit state (D-011).
6. Read toggle and rating control.

Convert the seed spreadsheet to CSV before step 4; the importer has nothing to
prove against until then.

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
  sentence and never explained. Development-flavoured anomalies preferred.
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

`ATTACK_VECTORS.md` is the canonical list — AV-001 to AV-012 cover enrichment
overwriting manual fields, double import, unsafe WAL backup, unenforced foreign
keys, the `times_read` trap, numeric position parsing, duplicate series
entries, joint-credit counting, provider quota, wrong-edition matches, and
export drift, and barcode misreads.

Not vectors, but worth knowing:

- **A re-read is read → reading → read.** The counter is driven by the state
  transition, not edited by hand, so any re-read control must perform both
  updates.
- **Series positions are not unique within a series.** An omnibus may share a
  position with its constituent volumes. The unique index covers series plus
  book, not series plus position.
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
