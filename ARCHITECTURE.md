# Architecture

Descriptive. Module boundaries, data flow and the invariants the system must
maintain. Rationale lives in `DECISIONS.md`; this document says what the system
is, not why.

Toolchain fixed by D-001: C++20, Qt6 Widgets, SQLite.

---

## 1. System overview

```
                   ┌──────────────────────────────────────────┐
                   │                   ui                     │
                   │  ┌────────┐ ┌──────────┐ ┌────────────┐  │
                   │  │  rail  │ │   list   │ │   detail   │  │
                   │  │ filter │ │  (table) │ │ view/edit  │  │
                   │  └────┬───┘ └────┬─────┘ └─────┬──────┘  │
                   │       └──────────┴─────────────┘         │
                   │              Qt item models              │
                   └──────────────────┬───────────────────────┘
                                      │ domain types
            ┌─────────────────────────┼─────────────────────────┐
            │                         │                         │
     ┌──────▼──────┐          ┌───────▼───────┐         ┌───────▼───────┐
     │     io      │          │      db       │         │   metadata    │
     │ import      │          │ repositories  │         │ GoogleBooks   │
     │ export      │◄─────────┤ migrations    ├────────►│ OpenLibrary   │
     │ backup      │          │ connection    │         │ cover cache   │
     └─────────────┘          └───────┬───────┘         └───────┬───────┘
                                      │                         │
                               ┌──────▼──────┐           ┌──────▼──────┐
                               │  pinax.db   │           │  covers/    │
                               │  (SQLite)   │           │  (on disk)  │
                               └─────────────┘           └─────────────┘
```

Everything flows through `db`. The UI never issues SQL; `metadata` and `io`
never touch the database file directly. One module owns the store.

---

## 2. Module responsibilities

### `domain`
Plain C++ types with no Qt or SQL dependency: `Book`, `Author`, `Series`,
`SeriesEntry`, `Genre`, and the enumerations for read state, binding, metadata
status and provenance source. Value types, copyable, no behaviour beyond
validation that is intrinsic to the type (ISBN check digits).

### `db`
Owns the SQLite connection and is the only module that writes SQL.

Uses the SQLite C API directly and has no Qt dependency (D-015).

- **Connection** — opens the database, asserts `PRAGMA foreign_keys = ON` on
  every connection (see invariant 6), sets WAL. `Statement` and `Transaction`
  are the only other ways SQL reaches SQLite.
- **Migrations** — applies `db/schema.sql`, compiled into the binary, to an
  empty file in one transaction, and thereafter steps forward by comparing
  `schema_version`, one `db/migrations/NNN_*.sql` file per version, each in its
  own transaction. `schema.sql` is always the latest full schema; a test
  proves that migrating a frozen version 1 file produces it exactly. Refuses
  a database newer than the build.
- **Repositories** — one per aggregate: `BookRepository`, `SeriesRepository`,
  `AuthorRepository`, `GenreRepository`. Each returns `domain` types, never
  result sets. Derived reads go through the views (`v_book_display`,
  `v_series_status`, `v_missing_entries`) rather than being recomputed in C++.

### `metadata`
Retrieval from external providers, and nothing else. No database access —
results are returned to the caller, which persists them through `db`.

- **Provider clients** — `GoogleBooksClient` primary, `OpenLibraryClient`
  fallback (D-008). Each maps a provider response onto a `domain::Candidate`.
- **Rate limiter** — serialises outbound requests and enforces a minimum
  interval, so batch enrichment stays inside the provider's daily quota.
- **Cover cache** — downloads an image once, writes it under `covers/`, returns
  the relative path. The database stores that path, never the image.

### `io`
Everything that crosses the application boundary as a file or a device.

No Qt; reaches the database only through `db`'s repositories and
`Savepoint`.

- **CSV import** — `parseCsv` reads RFC 4180; `CsvImporter` applies
  SPEC.md §1, resolving authors and series to existing rows, in one
  transaction with a savepoint per row so a failed row is reported with its
  line number and the rest still commit. `deriveSortPosition` here is the only
  code that reads a number out of a series position (invariant 3).
- **Export** — CSV of the current view, `.xlsx` workbook, plain-text SQL dump.
- **Backup** — `VACUUM INTO` to a chosen path.
- **Barcode capture** — webcam frames decoded to a validated ISBN (F-025,
  D-014). Optional at build time; emits an ISBN and touches nothing else.

### `ui`
Qt Widgets. Three panels inside a `QSplitter`, a toolbar and a status bar.

- **Rail** — a `QTreeView` over a filter tree. Selecting a node narrows the list.
- **List** — `BookListView`, a `QTableView` over `BookListModel` (a
  `QAbstractTableModel` of `domain::BookSummary`) through `BookSortProxy`. The
  composition root reads summaries from `BookRepository` and hands them over;
  `ui` does not link `db`. Sorting uses the view's sort keys, never the text
  on screen. Owns selection, sorting, and the single-key actions on the
  selection: R toggles read, 1–9 and 0 rate, Backspace or − clears the
  rating. It emits requests; it changes nothing itself.
- **Detail** — `DetailPanel` describes the current selection, whichever kind
  it is: nothing, a book, a series, or a multi-selection (which renders as the
  bulk editor). Carries both the view state (`BookView`) and the edit state
  (`BookEditor`) (D-011). Shown a `domain::BookDetail`; emits the edited
  `domain::Book`. Series completeness on show comes from the views, not from
  arithmetic in the panel (D-004).

### `app`
Composition root. Builds the modules, wires them together, owns settings
(window geometry, last filter, density) and the main window shell. The only
module permitted to know about all the others.

- **Catalogue** — the open database: one `Connection`, migrated on open, and
  the reads and writes the window needs, assembled from `db`'s repositories
  (`summaries`, `detail`, `save`, `toggleRead`, `setRating`, each write in
  one transaction). `save` turns a constraint failure into a
  sentence for the owner.
- **MainWindow** — the splitter. List selection drives the detail panel
  through the Catalogue; the panel's `saveRequested` is saved through it, and
  the saved row is refreshed in place.

---

## 3. Key invariants

1. **Series completeness is computed, never stored.** `v_series_status` derives
   held against known at query time. No status column exists on `series`, and
   adding one would reintroduce the staleness the views were built to avoid.
2. **`series_entry` is the sole join between books and series.** A row with
   `book_id IS NULL` is a known volume not owned. There is no second mechanism
   for recording what is missing.
3. **`position` is free text; `sort_position` is the sort key.** Nothing parses
   a number out of `position`.
4. **Authors are normalised.** A joint credit is several rows in `book_author`.
   No combined author record is ever created.
5. **A field whose source is `manual` is never overwritten by enrichment.**
   Provenance is checked before any fetched value is written.
6. **`PRAGMA foreign_keys = ON` is asserted on every connection.** SQLite does
   not persist it; without it the `REFERENCES` clauses are advisory.
7. **The database stores a cover path, not image data.** Images live under
   `covers/` and are addressed by book id.
8. **The UI issues no SQL.** All reads and writes go through a repository.

---

## 4. Cross-cutting concerns

**Threading.** Single-threaded by default. `QNetworkAccessManager` is
asynchronous on the UI thread, so metadata retrieval needs no worker thread;
batch enrichment is a state machine driven by reply signals, which is also what
makes it cancellable and resumable. Database access is synchronous — at a few
hundred rows, no query is long enough to be worth moving off the UI thread. Any
future long operation gets a worker, not a second connection on the UI thread.

**Error handling.** Expected failures — a row that will not import, a lookup
that finds nothing, a cover that will not download — are values, returned to the
caller and surfaced in the UI. Exceptions are reserved for programmer error and
unrecoverable state. A failed metadata fetch marks the book `failed` and leaves
existing content untouched; it never aborts a batch.

**Transactions.** Any operation touching more than one row runs inside a single
transaction: CSV import, bulk edit, and attaching a confirmed book to a waiting
`series_entry`. A partial import is a failed import.

**Logging.** Qt's categorised logging, one category per module
(`pinax.db`, `pinax.metadata`, `pinax.io`, `pinax.ui`). `db` has no Qt, so
what it throws is logged under `pinax.db` by its caller. Provider requests log
the URL and outcome but never a full response body.

**Settings.** `QSettings` for window state and view preferences only. Nothing
about the catalogue itself lives outside the database.
