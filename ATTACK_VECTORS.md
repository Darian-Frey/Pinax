# Attack Vectors

Project-specific failure modes Pinax must be resilient against.
Grouped by category. Each vector lists a detection method and a severity.

Severity: Critical (must hold) | Major (regression blocks release) | Minor (track only).

Detection is **defined, not necessarily implemented**. At the time of writing
no application code exists, so most entries read `not implemented`. That is
honest signal rather than a gap: each names the check that would catch it, which
is what the implementation owes.

---

## Data integrity

### AV-001 Enrichment overwrites a hand-entered field
**Severity:** Critical
**Description.** A metadata pass writes a fetched synopsis, publisher or genre
over a value the owner typed. The provider is wrong more often than the owner
is, and the overwritten value is unrecoverable — edition and condition notes in
particular hold facts no provider knows (a 1961 first English edition, a
misprinted spine).
**Detection.** Not implemented (would require a test asserting that a field
with `source = 'manual'` is unchanged after an enrichment pass over the same
book).
**Related decisions.** D-008, D-009.
**History.** Identified during schema design, 2026-10-04. The per-field
provenance columns exist for this reason.

### AV-002 Import run twice doubles the catalogue
**Severity:** Critical
**Description.** CSV import inserts rather than matching, so re-running the
seed file produces 886 books, duplicate authors and duplicate series entries.
Recovery means restoring a backup, which may not exist yet at the point the
mistake is most likely — the first import.
**Detection.** Not implemented (would require a test importing the same file
twice and asserting the row counts for `book`, `author` and `series_entry` are
unchanged after the second run).
**Related decisions.** D-007.
**History.** Identified while specifying F-003 idempotency, 2026-10-04.

### AV-003 Unsafe backup of a WAL-mode database
**Severity:** Critical
**Description.** The schema sets `journal_mode = WAL`. A plain file copy of
`pinax.db` taken while the application is running omits the write-ahead log, so
the copy is stale or corrupt. A backup that silently is not one is worse than
no backup.
**Detection.** Not implemented (would require a test that writes during a
backup, then opens the copy and asserts the pre-backup row count and integrity
check pass).
**Related decisions.** D-002.
**History.** Identified during schema design, 2026-10-04. `VACUUM INTO` is
specified in SPEC.md §5.4 for this reason.

### AV-004 Foreign keys silently unenforced
**Severity:** Major
**Description.** `PRAGMA foreign_keys` is per-connection in SQLite and is not
persisted by the schema file. A connection opened without it treats every
`REFERENCES` clause as advisory, so cascades do not fire and orphan rows
accumulate unnoticed — a deleted book leaves its credits and series entries
behind.
**Detection.** Not implemented (would require a test deleting a book and
asserting no `book_author` or `book_genre` rows survive, plus an assertion at
connection open).
**Related decisions.** D-001, D-002.
**History.** Identified during schema design, 2026-10-04.

---

## Catalogue correctness

### AV-005 `times_read` zero for imported read books
**Severity:** Major
**Description.** `trg_book_finished` increments the re-read counter on
`UPDATE` only. A book inserted directly with `read_status = 'read'` keeps
`times_read = 0`, so importing the 176 already-read books leaves every one of
them recorded as never finished. The error is quiet and only visible later,
when the counter is trusted.
**Detection.** Not implemented (would require a test asserting that a book
imported as read has `times_read >= 1`).
**Related decisions.** —
**History.** Found during schema verification on 2026-10-04, by inserting a
read book directly and observing the counter stay at zero. Recorded in
SPEC.md §1.3 and CLAUDE.md §7 before any importer existed.

### AV-006 Series position parsed as a number
**Severity:** Major
**Description.** Code that reads `position` as an integer — to sort, to find
the next volume, to label a row — breaks on every non-numeric form the real
collection contains: `Broadcast 6.5`, `1-4`, `3a`, `novellas`, `companion`.
Failures are silent where parsing yields a partial number rather than an error.
**Detection.** Not implemented (would require a test ordering a series
containing each form and asserting the sequence, plus a grep-level review that
`position` is never passed to a numeric conversion outside the importer).
**Related decisions.** D-005.
**History.** Identified while cataloguing Spinward Fringe, whose Broadcast 6.5
entry is the canonical case.

### AV-007 Duplicate record for a volume already recorded as missing
**Severity:** Major
**Description.** Adding a book that fills a known gap creates a second
`series_entry` instead of attaching to the waiting one. The series then shows
eleven entries where ten exist, never reports complete, and the ghost row
persists beside the book that was meant to replace it.
**Detection.** Not implemented (would require a test adding a book whose series
and position match an entry with `book_id IS NULL`, then asserting the entry
count is unchanged and `v_series_status` reports Complete).
**Related decisions.** D-004, D-006, D-012.
**History.** Identified while specifying F-024, 2026-10-04.

### AV-008 Joint credits counted as separate authors
**Severity:** Minor
**Description.** Treating a credit string as an author name makes
"Terry Pratchett" and "Terry Pratchett & Neil Gaiman" two different authors, so
every per-author total is wrong and filtering by an author misses their
collaborations. This is a realised defect in the source spreadsheet, carried in
if the importer does not split credits.
**Detection.** Not implemented (would require a test importing a joint credit
and asserting both authors resolve to existing rows, with per-author counts
including the collaboration).
**Related decisions.** D-007.
**History.** Observed in the seed data during cataloguing, 2026-08-30.

---

## External dependencies

### AV-009 Provider quota exhausted mid-run
**Severity:** Major
**Description.** Google Books allows roughly a thousand unauthenticated
requests per day per IP. Enriching 443 books with a cover fetch each approaches
that in one pass. A run that is not resumable loses its progress and burns the
remaining quota retrying books already done.
**Detection.** Not implemented (would require a test interrupting a batch and
asserting that resuming skips books already marked `matched`, plus handling of
HTTP 429 as a pause rather than a failure).
**Related decisions.** D-008.
**History.** Identified while specifying Phase 3, 2026-10-04.

### AV-010 Wrong edition or wrong book matched
**Severity:** Major
**Description.** A title-and-author search returns a plausible but wrong
result — a different edition, a same-titled book, an omnibus in place of a
single volume — and is accepted without review. The catalogue then holds
confident, wrong data, which is worse than an empty field because nothing
prompts a correction. Most acute for the 443 seeded books, none of which carry
an ISBN.
**Detection.** Not implemented (would require title-and-author matches to be
queued for confirmation rather than auto-accepted, and a report listing books
whose metadata was accepted on a fuzzy match).
**Related decisions.** D-008, D-012.
**History.** Identified while specifying the matching strategy in SPEC.md §3.3,
2026-10-04.

---

## Export fidelity

### AV-011 Exported figures disagree with the application
**Severity:** Minor
**Description.** An export that recomputes series completeness in its own code
rather than reading `v_series_status` will drift from what the application
shows. Two sources of truth for one fact, in breach of the documentation
standard's Maintenance Rule 5.
**Detection.** Not implemented (would require a test comparing the exported
workbook's series counts against the view's, row for row).
**Related decisions.** D-004.
**History.** Identified while specifying SPEC.md §5.1, 2026-10-04.

---

## Barcode capture

### AV-012 Wrong barcode read as an ISBN
**Severity:** Minor
**Description.** The webcam decodes something other than the book's ISBN — a
second-hand shop's price sticker, the 5-digit price add-on appended to the
main code, or a partial read of a blurred barcode — and the result is looked
up or, worse, assigned to an existing book during backfill. The wrong ISBN
then drives every later metadata fetch for that record.
**Detection.** Not implemented (would require tests feeding the decoder a
non-978/979 EAN-13, an EAN-13 with add-on, and a sequence of frames that
disagree, and asserting that none yields an accepted ISBN).
**Related decisions.** D-014, D-012.
**History.** Identified while planning F-025, 2026-10-05. The capture rules in
SPEC.md §6 exist for this reason; the confirmation step of D-012 is the last
line of defence.
