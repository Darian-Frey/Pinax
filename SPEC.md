# Specification

Authoritative technical reference for the formats and protocols Pinax reads and
writes. `FEATURES.md` says what the application does; this says exactly how.

The database schema is **not** restated here. `db/schema.sql` is its single
source of truth; this document covers everything else.

---

## 1. CSV import format

The seed and interchange format for F-003. UTF-8, comma-separated, first row is
a header, quoting per RFC 4180.

| Column | Required | Type | Notes |
|---|---|---|---|
| `title` | yes | text | — |
| `subtitle` | no | text | — |
| `authors` | no | text | Credits separated by ` & `. See below. |
| `series` | no | text | Series name. Created if unknown. |
| `position` | no | text | Verbatim, including `Broadcast 6.5`, `1-4`, `3a`. |
| `sort_position` | no | real | Omitted: derived by the rule below. |
| `shelf` | no | enum | `read` \| `unread` \| `reading` \| `abandoned`. Default `unread`. |
| `times_read` | no | integer | **Set explicitly for read books.** Omitted or empty on a new book: 1 if `shelf` is `read`, else 0. See §1.3. |
| `rating` | no | integer | 1–10, or empty for unrated. |
| `isbn13` | no | text | Validated per §2. |
| `publisher` | no | text | — |
| `published_year` | no | integer | — |
| `binding` | no | enum | `paperback` \| `hardback` \| `omnibus` \| `boxset` \| `other`. |
| `edition_note` | no | text | — |
| `condition_note` | no | text | — |
| `notes` | no | text | — |

### 1.1 Author splitting
`authors` splits on ` & ` (space-ampersand-space). Each part becomes an
`author` row if absent, and a `book_author` link with `ordinal` set to its index
and `role` defaulting to `author`.

A part may carry an explicit role as a trailing parenthesis:
`Mike Ashley (editor)`. Recognised roles are `author`, `editor`, `translator`,
`illustrator`.

The same notation is how credits are typed in the detail panel's edit form;
`domain::parseCredits` and `domain::formatCredits` implement it once for both.

**Known limitation.** A single author whose name contains ` & ` would be split
incorrectly. No such case exists in the seed data; if one appears, the row needs
hand-correcting after import.

### 1.2 `sort_position` derivation
When `sort_position` is empty, derive it from `position`:

1. Leading number, possibly decimal, possibly after a word — `6`, `6.5`,
   `Broadcast 6.5` → that number.
2. A range — `1-4`, `3-4` → the first number.
3. A number with a letter suffix — `3a`, `3b` → the number plus `0.1` × the
   letter's position in the alphabet (`3a` → 3.1, `3b` → 3.2).
4. Anything else — `novellas`, `collection`, `companion`, `universe` → null,
   which sorts last.

The derivation runs at import only. Thereafter `sort_position` is a stored
value and is never recomputed from `position`.

### 1.3 Idempotency and the re-read trap
Import matches an existing book on `isbn13` where present, otherwise on
case-folded `title` plus the first-billed author's name (ASCII case folding,
as SQLite's `lower()`). A title-and-author match is refused if the existing
book carries a different ISBN-13, and the row then inserts. A match is
updated only where the file differs from what is stored, so re-running the
same file changes nothing — not even `updated_at`. A miss inserts. Matching is therefore only as stable as the credits: a book
whose credits were corrected in the application no longer matches an older
file that still carries the old ones, and re-importing that file inserts it
again (AV-013). Regenerate the file before re-importing.

Columns absent from the header leave their fields alone on an update; a
column present with an empty cell clears the field. Authors and series are
reused by exact name. A new author's `sort_name` is derived surname-first,
keeping a particle (`de`, `del`, `le`, `van`, `von` and similar) with the
surname: `Jon Del Arroz` files as `Del Arroz, Jon`. It is never recomputed.

A series position that matches an entry in that series with no book
attached fills that entry rather than adding a second one (AV-007). A row
naming a different series from last time adds an entry; it does not remove
the old one.

`times_read` **must be written directly** by the importer. The
`trg_book_finished` trigger fires on `UPDATE` only, so a book inserted with
`read_status = 'read'` keeps `times_read = 0` (AV-005). Conversely, an update
that moves a book into `read` fires the trigger, which overrides the count;
when the file gives `times_read`, the file's value is written afterwards.

### 1.4 Errors

The header is checked first. An unknown column, a repeated column or a
missing `title` column stops the run before anything is written.

The run is one transaction, with a savepoint per row. A row that fails —
wrong field count, empty title, unrecognised `shelf` or `binding`, a number
that does not parse, a rating outside 1–10, an ISBN-13 failing its check
digit, a credit role not in §1.1, a `position` with no `series`, a constraint
such as a duplicate ISBN-13, or a second row resolving to a book an earlier
row of the same file already wrote — is rolled back alone and reported with
the physical line it starts on. The one exception to the last (D-026): a
further row for a book that changes nothing about it and names a series not
yet given for it in this file adds that series. A book in several series is
a row per series. Any other database error rolls back the whole
run; a partial import is a failed import.

### 1.5 Running an import

```sh
pinax --import seed/library.csv [database]
```

Failures print as `file:line: message`; the summary (new, updated, unchanged,
failed) goes to standard output and the status bar (D-016).

### 1.6 Series CSV — known volumes and ongoing series

What a series contains beyond the shelf (D-006, D-018). Same encoding and
quoting as §1. Imported with `pinax --import-series series.csv`, after
`--import` when both are given.

| Column | Required | Type | Notes |
|---|---|---|---|
| `series` | yes | text | Series name, exact. Created if unknown. |
| `position` | no | text | As printed: `1`, `Broadcast 6.5`, `3-4`, `novella`. |
| `sort_position` | no | real | Omitted: derived from `position` by §1.2. |
| `title` | no | text | The volume's title, where known. |
| `ongoing` | no | `yes` \| `no` | Sets the series' flag; empty leaves it alone. |
| `notes` | no | text | Notes on the volume. |

A row with a `position` or `title` is a volume the series contains. A row
with only `ongoing` is a flag on the series. A row may be both.

**Matching.** Within the series, a volume is matched on `position` when it
has one, otherwise on `title` among entries with no position, without regard
to ASCII case.
- No match: a volume with no book attached is added.
- A match with no book: its title, sort position and notes are brought into
  line with the file where the file gives them.
- A match with a book: the volume is on the shelf; the row is left alone
  (AV-007).

Nothing is removed: a volume dropped from the file stays in the catalogue
until removed in the application. Re-running the same file changes nothing.

**Errors**, reported by line as in §1.4: an empty `series`; a row with no
`position`, `title` or `ongoing`; `notes` with no volume; `ongoing` other
than `yes` or `no`; a `sort_position` that is not a number; and an
`ongoing` that contradicts an earlier row of the same file for the same
series. An unknown or repeated column, or no `series` column, stops the run
before anything is written.

---

## 2. ISBN validation

### ISBN-13
Thirteen digits. Weights alternate 1, 3 across the first twelve; the sum plus
the check digit must be divisible by 10.

```
sum = Σ d[i] × (i even ? 1 : 3)   for i = 0..11
check = (10 − sum mod 10) mod 10
valid ⟺ check == d[12]
```

### ISBN-10
Nine digits plus a check character. Weights run 10 down to 2; the check
character is `X` for 10. The weighted sum plus the check value must be
divisible by 11.

```
sum = Σ d[i] × (10 − i)           for i = 0..8
check = (11 − sum mod 11) mod 11   (10 renders as 'X')
valid ⟺ check == value(d[9])
```

Hyphens and spaces are stripped before validation. Storage is digits only.

---

## 3. Metadata providers

Provider order is D-019: Open Library first, Google Books only with an API
key; the British Library fills the gaps in an ISBN answer (D-022, §3.6).
Every request goes through one `RequestQueue` per provider (D-020).

### 3.1 Open Library — primary

No key. An ISBN lookup is up to three requests:

```
GET https://openlibrary.org/api/books?bibkeys=ISBN:{isbn13}&format=json&jscmd=data
GET https://openlibrary.org{edition key}.json       the edition: its work, perhaps a synopsis
GET https://openlibrary.org{work key}.json          the work: a synopsis, if the edition had none
```

An empty object (`{}`) from the first means the ISBN is not held: a miss,
not an error. A title-and-author search is one request and returns works:

```
GET https://openlibrary.org/search.json?title={title}&author={author}&limit=5
    &fields=key,title,author_name,first_publish_year,isbn,cover_i,number_of_pages_median,publisher,subject
```

| Response field (Books API / search) | Target |
|---|---|
| `title` | the candidate card; never written over the book's own |
| `subtitle` | the candidate card |
| `authors[].name` / `author_name[]` | the candidate card; credits are the owner's |
| `publishers[0].name` / `publisher[0]` | `book.publisher` |
| `publish_date` (first four-digit year) | `book.published_year` |
| `first_publish_year` (search) | first-published year on the confirmation card (F-024) |
| `number_of_pages` / `number_of_pages_median` | `book.page_count` |
| `identifiers.isbn_13`, `identifiers.isbn_10` | the candidate card; written only by add-by-ISBN (F-024), never by a fetch |
| `subjects[].name` / `subject[]` | `genre` rows, verbatim, source `open_library` |
| `cover.large` / `cover_i` | the cover cache (§4), source `open_library` |
| `description` (edition or work) | `book.synopsis`, source `open_library` |

A `description` is either a string or `{"type": "/type/text", "value": "…"}`;
both are read. Search results never carry one; the work is fetched for it once
a candidate is confirmed.

Covers come from `covers.openlibrary.org/b/id/{cover id}-L.jpg`, by cover id
rather than by ISBN.

### 3.2 Google Books — with a key only

```
GET https://www.googleapis.com/books/v1/volumes?q=isbn:{isbn13}&key={key}
GET https://www.googleapis.com/books/v1/volumes?q=intitle:{title}+inauthor:{author}&key={key}
```

Without a key nothing is sent: since at least 2026-10-06 a keyless request
is refused with HTTP 429 and a daily quota of 0. With a key, on 2026-10-06,
field-qualified queries (`isbn:`, `intitle:`, `inauthor:`) returned no
results — or "Service temporarily unavailable" — even for books Google
holds, while free-text queries answered normally (IMP-007). Fields consumed from
`items[].volumeInfo`: `title`, `subtitle`, `authors[]`, `publisher`,
`publishedDate` (leading four digits), `description`, `pageCount`,
`categories[]` (verbatim, so `Fiction / Science Fiction / Space Opera` is one
genre row, D-009), `industryIdentifiers[]`, and `imageLinks.thumbnail`
(requested over https). Source `google_books`.

### 3.3 Politeness

Requests to a provider start at least a second apart. HTTP 429 or 503 pauses
that provider's queue — for `Retry-After` seconds if given, otherwise 30
seconds doubling to at most 10 minutes — and the same request is retried.
Other 5xx answers and network failures are retried on the same backoff. A
request is given up after five attempts and its failure reported; a 404 is
reported at once. Requests identify themselves as
`Pinax/<version> (personal library catalogue)` and carry nothing about the
owner.

### 3.4 Matching strategy

1. ISBN present → ISBN lookup. A single result is accepted automatically during
   batch enrichment if its title agrees with the book's (`domain::titlesAgree`,
   D-023) — otherwise it is queued for review — and shown for confirmation
   during add-by-ISBN (F-024).
2. No ISBN → title and author search. **Never** accepted automatically; the
   candidate is queued for manual confirmation. See AV-010.
3. No result from any provider → `metadata_status = 'failed'`, existing
   content untouched. A provider that could not be reached is not a "no
   result": the book is left as it was and the problem reported; a batch run
   stops there and resumes from it (D-023).
4. Rejected in review (Not my book) → `metadata_status = 'failed'`, content
   untouched; later batch runs skip it.

Within each step Open Library is asked first and Google Books, with a key,
only if Open Library has nothing (D-019); for an ISBN the British Library is
asked alongside Open Library (§3.6); an ISBN neither knows falls through
to step 2. An ISBN-10 is asked as its ISBN-13. Fetch metadata in the panel
(F-012) always shows the candidates and writes nothing until one is chosen —
even for an ISBN, which can name another book (AV-010); an ISBN's answer is
offered ready to accept, a search's never is.

### 3.5 What a chosen candidate writes

`domain::planEnrichment` is the one statement of these rules (AV-001):

| Field | Rule |
|---|---|
| `synopsis`, `synopsis_source` | written with the provider as source, unless the source is `manual` |
| `published_year` | filled if empty: the work's first-published year, else the edition's |
| `publisher`, `page_count` | filled if empty, and **only from an ISBN lookup** — a search result may describe another edition |
| `isbn13`, `isbn10` | never |
| `title`, `subtitle`, credits, series | never |
| edition and condition notes, acquisition, notes | never |
| read state, `times_read`, rating | never |
| genres | each category added verbatim with the provider as source — those from a gap-filling provider under its own name; a genre already linked keeps its source, so the owner's stay `manual` |
| cover | fetched into the cache (§4) unless `cover_source` is `manual` |
| `metadata_status`, `metadata_fetched_at` | `matched` and the time, UTC; a status of `manual` stays |

Publisher, year and page count have no source column, so an existing value
may be the owner's and is never replaced. The writes happen in one
transaction; the cover follows when it has downloaded.

### 3.6 British Library — UK editions, by ISBN

No key. One SRU request per ISBN to the British Library's Alma catalogue:

```
GET https://bl.alma.exlibrisgroup.com/view/sru/44BL_MAIN?version=1.2
    &operation=searchRetrieve&recordSchema=marcxml&maximumRecords=5
    &query=alma.isbn={isbn13}
```

`numberOfRecords` 0 is a miss, not an error; an SRU diagnostic is an error.
An ISBN-13 query also finds records that give only the ISBN-10. The index
returns related editions too (an ebook beside a paperback), so a record is
kept only if one of its own `020 $a` ISBNs — the digits before any
qualifier, an ISBN-10 converted — is the one asked.

| MARC 21 field | Target |
|---|---|
| `245 $a`, `$b` | title and subtitle on the card, closing punctuation (` /`, ` :`) removed |
| `100`, `700 $a` with no `$e` or `$e` author | authors on the card, "Baxter, Stephen," as "Stephen Baxter" |
| `264` (second indicator 1) or `260 $b` | publisher |
| `008/07-10` | publication year |
| `008/11-14` when `008/06` is `r` | first-published year: a reprint's original |
| `300 $a`, the number before "pages" or "p." | page count |
| `650 $a`, `655 $a` | genres, verbatim but for MARC's closing full stop; source `british_library` |

No synopsis and no cover. Where Open Library also answers the ISBN, its
candidate stands and these fill only its empty publisher, page count, years
and subtitle; the British Library's genres are added beside Open Library's
(D-022). The candidate card names both providers.


### 3.7 Adding by ISBN — F-024

1. The typed ISBN is normalised and checked (§2); an ISBN-10 is looked up as
   its ISBN-13 and both are kept. A bad check digit stops here.
2. An ISBN already held, as ISBN-13 or its ISBN-10 form, is reported with
   the book; nothing is looked up.
3. Open Library and the British Library are asked together, then Google with
   a key (§3.4 step 1). No title search follows by itself.
4. The card, per candidate: cover (downloaded, checked, not yet kept), title
   and authors (editable; known authors' spellings substituted), publisher,
   edition and first-published years, pages, series statement, providers.
5. A held book with no ISBN, an agreeing title and a shared author is
   offered: giving it the ISBN writes `isbn13`/`isbn10` and the candidate's
   details as a fetch would (§3.5); nothing else of the book changes.
6. Otherwise, series proposals (D-024), then Unread or Read. Adding writes
   the book — title, subtitle, credits, ISBN, read state from the card —
   then the candidate under §3.5, then the series: the waiting entry takes
   the book, or a new entry is made at the position and sort number shown.
7. The cover shown is the cover kept: the same bytes are written to the
   cache (§4), not downloaded again.
8. Not this book → a title-and-author search (prefilled, editable); its
   candidates start unchosen, and one taken writes only what a search may
   (§3.5) beside the typed ISBN. Nothing found → the ordinary form,
   prefilled with the ISBN, title and authors known so far.

---

## 4. Cover cache

```
<data dir>/covers/<book_id>.<ext>
```

`<data dir>` is the directory holding `pinax.db`. `<ext>` is `jpg` or `png`,
from the downloaded bytes' signature rather than the URL or content type.
`book.cover_path` stores the path relative to the data directory, so moving
the pair together keeps the catalogue intact.

A download is kept only if it is a JPEG or PNG of at least 1 KiB — a 1×1
placeholder or an HTML error page is refused — and is written atomically, so
a failure leaves no partial file. Open Library cover URLs are always requested
with `default=false`, so a missing cover is a 404 rather than a blank image.

A cover already present on disk is not re-downloaded (F-013). Deleting the
cache is safe: the next enrichment pass refetches, and a book with a missing
file displays a placeholder rather than failing.

Deleting a book deletes its cover file. SQLite may give a later book the same
id, and that book must not inherit someone else's cover.

Covers shown before a choice — a thumbnail beside each candidate in Fetch
metadata and Review matches, the cover on the Add by ISBN card — are
downloaded and checked like any other but kept only in memory. Thumbnails
use Open Library's medium size (`-M.jpg` for `-L.jpg`); other providers'
images are used as given. They queue behind nothing else's urgency: as soon
as the choice is made or abandoned, those not yet downloaded are withdrawn
from the queue, so the chosen book's own cover is not kept waiting.

`book.cover_source` records where the cover came from. A cover whose source is
`manual` is never replaced by a fetched one (AV-001).

---

## 5. Export formats

### 5.1 Excel — F-022

Three sheets, matching the structure the catalogue was seeded from.

**Books** — `#`, `Title`, `Author`, `Series`, `Vol.`, `Shelf`,
`Series status`, `Still missing`, `Rating`, `Times read`, `Year`, `Publisher`,
`ISBN`.

**Series status** — `Series`, `Author`, `Total held`, `Read`, `Unread`,
`Status`, `Still missing`. Values come from `v_series_status`, so the workbook
and the application agree by construction.

**Authors** — `Author`, `Books held`, `Read`, `Unread`.

Written with libxlsxwriter (D-025). Headers present; no formulas — the figures are the
computed values at export time.

As built: Books runs in the list's order — author, then series and
position — with a book's first series where it has several; `Shelf` is
Read, Unread, Reading or Abandoned; `Series status` and `Still missing` are
that series' (`v_series_status`, `v_missing_entries`). `Still missing` lists
named volumes with their positions and counts placeholders, as the panel
does: "Consider Phlebas (1); and 2 not yet identified". On Series status,
`Author` is the series' usual credits. Authors counts author credits only,
never an editor's (IMP-004). Headers are bold, frozen and filterable;
numbers are numbers; an absent value is an empty cell. Written to
`<file>.partial`, renamed into place on success.

### 5.2 CSV — F-023
The current view, filters and sort applied, in the column order of §1, so a
round trip through F-003 loses nothing.

- Rows: the books the list shows, in its order — filters, search and sort
  applied; group headings left out, and a book under two genres written
  once. `pinax --csv` writes every book in the list's opening order.
- Columns: all of §1's, in its order, with the header. `authors` in the
  credit notation of §1.1, roles included; `shelf`, `binding` as their enum
  words; `times_read` and `sort_position` always written, so nothing is
  re-derived on import (AV-005, AV-006); reals with the fewest digits that
  round-trip. RFC 4180 quoting, only where needed.
- A book in several series is a row per series, identical but for `series`,
  `position` and `sort_position` (D-026).
- Not carried, because §1 has no column for them: ISBN-10, synopsis,
  genres, covers, dates, acquisition. The SQL dump (§5.3) carries
  everything.

Written to `<file>.partial`, imported into an empty catalogue, exported
again from there, and kept only if the two files are identical. Two books
the importer would take for one — same title and first author, no ISBN —
refuse the export by line, rather than lose a book.

### 5.3 SQL dump — F-021
Plain text, equivalent to `sqlite3 pinax.db .dump`: schema plus `INSERT`
statements, readable and diffable, restoring onto an empty database.

- A comment naming the schema version, then `PRAGMA foreign_keys=OFF;` and
  `BEGIN TRANSACTION;`.
- Each table in schema order: its `CREATE TABLE`, then its rows ordered by
  key, one `INSERT INTO t VALUES(…);` a row. Then indexes, views and triggers,
  the triggers last so that no restored row fires one. `COMMIT;`.
- Values: `NULL`; integers as written; reals with the fewest digits that
  give the same double back, always with a point (`6.5`, `5.0`); text in
  single quotes with `''` for a quote, newlines and tabs as they are, any
  other control character spelt `char(n)` joined with `||` — a bare carriage
  return would not survive editors, git or the sqlite3 shell; blobs as
  `X'…'`.
- Nothing run-dependent — no date, no file name — so an unchanged catalogue
  dumps to the same bytes.

Written to `<file>.partial`, restored into an empty in-memory database, and
kept only if every table's row count matches and the restored catalogue
dumps back to the identical text; then renamed over `<file>`. `--dump -`
writes to standard output unchecked, for piping.

### 5.4 Backup — F-020
`VACUUM INTO '<path>'`. Produces a consistent copy while the application is
running. A plain file copy of a WAL-mode database is not safe (AV-003).

`db::backupTo`, behind Back up (Ctrl+B) and `pinax --backup <file>`:

1. Folders on the way to `<file>` are created; a folder for a file name is
   refused.
2. `VACUUM INTO '<file>.partial'` — any stale `.partial` removed first, since
   `VACUUM INTO` will not write over a file.
3. The partial file is opened read-only and checked: `PRAGMA quick_check`
   says `ok`, and its book count and highest `schema_version` equal the live
   catalogue's.
4. Only then is it renamed to `<file>`, replacing any file there. On any
   failure the partial file is removed and `<file>` is untouched.

The copy is a single file in rollback-journal mode, with no `-wal` or `-shm`
beside it. Covers are not copied (§4). `--backup` prints one line and exits 0
on success, 1 otherwise, without opening the window.

---

## 6. Barcode capture — F-025

Webcam frames are decoded for **EAN-13 only**; other symbologies are not
enabled, so a QR code or a shop's own Code 128 sticker is never read.

A decoded value is accepted as an ISBN when all of the following hold:

1. Thirteen digits, prefix `978` or `979`. Any other EAN-13 is ignored — on
   second-hand books this is usually a retailer's price sticker.
2. Valid ISBN-13 check digit per §2.
3. The same value decoded in **three consecutive frames**.

A 5-digit supplement (the price add-on printed beside the main barcode on many
UK and US paperbacks) is discarded; only the 13-digit main code is kept.

After an accepted read, the same value is ignored for 2 seconds, so a barcode
left in front of the camera does not trigger a second lookup.

Frames are scaled to at most 1280 pixels on the long edge before decoding.
