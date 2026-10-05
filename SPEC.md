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
the physical line it starts on. Any other database error rolls back the whole
run; a partial import is a failed import.

### 1.5 Running an import

```sh
pinax --import seed/library.csv [database]
```

Failures print as `file:line: message`; the summary (new, updated, unchanged,
failed) goes to standard output and the status bar (D-016).

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

### 3.1 Google Books — primary

```
GET https://www.googleapis.com/books/v1/volumes?q=isbn:{isbn13}
GET https://www.googleapis.com/books/v1/volumes?q=intitle:{title}+inauthor:{author}
```

Fields consumed from `items[0].volumeInfo`:

| Response field | Target |
|---|---|
| `title` | `book.title` |
| `subtitle` | `book.subtitle` |
| `authors[]` | `author` rows via `book_author` |
| `publisher` | `book.publisher` |
| `publishedDate` | `book.published_year` (leading 4 digits) |
| `description` | `book.synopsis`, source `google_books` |
| `pageCount` | `book.page_count` |
| `categories[]` | `genre` rows via `book_genre`, source `google_books` |
| `imageLinks.thumbnail` | downloaded to the cover cache, source `google_books` |
| `industryIdentifiers[]` | `book.isbn13`, `book.isbn10` |

Categories are stored **verbatim** — `Fiction / Science Fiction / Space Opera`
is one genre row, not three (D-009).

**Quota.** Roughly 1,000 requests per day per IP unauthenticated. Batch
enrichment must be resumable rather than all-or-nothing (AV-009).

### 3.2 Open Library — fallback

Used when Google Books returns no match, or returns a match with no cover.

```
GET https://openlibrary.org/api/books?bibkeys=ISBN:{isbn}&format=json&jscmd=data
GET https://covers.openlibrary.org/b/isbn/{isbn}-L.jpg
```

Cover sizes are `S`, `M`, `L`; request `L`. A missing cover returns a 1-pixel
placeholder image unless `?default=false` is appended, which returns 404
instead — always append it, so a miss is detectable.

`subjects[]` may be consumed as genres, source `open_library`, but is noisier
than Google's categories and is a fallback only.

### 3.3 Matching strategy

1. ISBN present → ISBN lookup. A single result is accepted automatically during
   batch enrichment, and shown for confirmation during add-by-ISBN (F-024).
2. No ISBN → title and author search. **Never** accepted automatically; the
   candidate is queued for manual confirmation. See AV-010.
3. No result from either provider → `metadata_status = 'failed'`, existing
   content untouched.

---

## 4. Cover cache

```
<data dir>/covers/<book_id>.<ext>
```

`<data dir>` is the directory holding `pinax.db`. `<ext>` follows the response
content type, `jpg` or `png`. `book.cover_path` stores the path relative to the
data directory, so moving the pair together keeps the catalogue intact.

A cover already present on disk is not re-downloaded. Deleting the cache is
safe: the next enrichment pass refetches, and a book with a missing file
displays a placeholder rather than failing.

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

Written with libxlsxwriter. Headers present; no formulas — the figures are the
computed values at export time.

### 5.2 CSV — F-023
The current view, filters and sort applied, in the column order of §1, so a
round trip through F-003 loses nothing.

### 5.3 SQL dump — F-021
Plain text, equivalent to `sqlite3 pinax.db .dump`: schema plus `INSERT`
statements, readable and diffable, restoring onto an empty database.

### 5.4 Backup — F-020
`VACUUM INTO '<path>'`. Produces a consistent copy while the application is
running. A plain file copy of a WAL-mode database is not safe (AV-003).

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
