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
**Detection.** Implemented, 2026-10-06. The rules are one pure function,
`domain::planEnrichment` (SPEC.md §3.5). `tests/test_domain.cpp`,
`enrichmentNeverTouchesWhatTheOwnerWrote`, gives it a book with every
owner field set and a candidate with an answer for each, and finds the book
unchanged but for its match status; `tests/test_catalogue.cpp`,
`enrichingNeverOverwritesTheOwnersWork`, does the same through the database —
manual synopsis, publisher, year, pages, ISBN, edition note, notes, a manual
cover and a manual genre — and `aHandSetCoverIsNeverReplaced` covers
`Catalogue::setCover`. `tests/test_detail_panel.cpp`,
`editedSynopsisIsMarkedManual`, checks hand edits are marked.
**Related decisions.** D-008, D-009.
**History.** Identified during schema design, 2026-10-04. The per-field
provenance columns exist for this reason.

### AV-002 Import run twice doubles the catalogue
**Severity:** Critical
**Description.** CSV import inserts rather than matching, so re-running the
seed file produces 886 books, duplicate authors and duplicate series entries.
Recovery means restoring a backup, which may not exist yet at the point the
mistake is most likely — the first import.
**Detection.** Implemented, 2026-10-05. `tests/test_import.cpp`:
`secondRunChangesNothing` imports twice and asserts the counts for `book`,
`author`, `book_author`, `series` and `series_entry`, and that no
`updated_at` moved; `seedCatalogueImportsInOnePass` does the same against the
real 443-book seed where it is present.
**Related decisions.** D-007.
**History.** Identified while specifying F-003 idempotency, 2026-10-04.

### AV-003 Unsafe backup of a WAL-mode database
**Severity:** Critical
**Description.** The schema sets `journal_mode = WAL`. A plain file copy of
`pinax.db` taken while the application is running omits the write-ahead log, so
the copy is stale or corrupt. A backup that silently is not one is worse than
no backup.
**Detection.** Implemented, 2026-10-07. `tests/test_db.cpp`,
`aBackupHoldsWhatTheLogHolds`, writes rows that stay in the log, shows a
plain file copy missing them — or missing the tables outright — and the
backup holding every one, passing the integrity check at the latest schema
version; `aFailedBackupLeavesTheOldOneAlone` shows a refused backup leaves
the previous one untouched and no partial file behind. `db::backupTo`
checks every copy itself before keeping it.
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
**Detection.** Implemented, 2026-10-05. `db::Connection` enables foreign keys
on open and throws if `PRAGMA foreign_keys` does not then read back as 1.
`tests/test_db.cpp`: `foreignKeysEnforcedOnEveryConnection` opens the same
file twice and checks both the setting and an actual rejected insert;
`deletingABookLeavesNoOrphanLinks` deletes a book and asserts no
`book_author` or `book_genre` rows survive.
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
**Detection.** Implemented, 2026-10-05. `tests/test_db.cpp`,
`importedReadBookKeepsItsTimesRead`, at the repository; `tests/test_import.cpp`,
`readBooksCarryTheirCount` (explicit, defaulted and unread) and
`changedRowsUpdateInPlace` (the trigger's override corrected) at the
importer; `seedCatalogueImportsInOnePass` asserts no read book in the real
seed is left below 1.
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
**Detection.** Partly implemented, 2026-10-05. `tests/test_book_list.cpp`,
`seriesSortsBySortPositionNotPrintedPosition`, orders a series by
`sort_position` with `10`, `6.5` and a non-numeric `companion` among the
printed positions and asserts the sequence. The grep-level review that
`position` is never passed to a numeric conversion outside the importer is
still owed, as is the importer's own derivation test (SPEC.md §1.2).
**Related decisions.** D-005.
**History.** Identified while cataloguing Spinward Fringe, whose Broadcast 6.5
entry is the canonical case.

### AV-007 Duplicate record for a volume already recorded as missing
**Severity:** Major
**Description.** Adding a book that fills a known gap creates a second
`series_entry` instead of attaching to the waiting one. The series then shows
eleven entries where ten exist, never reports complete, and the ghost row
persists beside the book that was meant to replace it.
**Detection.** Partly implemented, 2026-10-05 and 2026-10-06.
`tests/test_import.cpp`, `fillsAWaitingMissingVolume`, imports a book into a
waiting position and asserts the entry count is unchanged and
`v_series_status` moves to Complete. `tests/test_catalogue.cpp` does the same
for Mark as owned: `attachingFillsTheWaitingEntry` and
`markOwnedWithABookAlreadyHeld` attach an existing book,
`aNewBookForAMissingVolumeTakesItsEntry` and `markOwnedWithANewBook` create
one into the entry, and `attachingIsRefusedWhereItWouldDuplicate` refuses an
owned entry or a book already in the series. The add-by-ISBN path (F-024):
`addingFillsTheMissingVolumeItMatches` and
`addingByIsbnFillsTheGapThroughTheWindow` add into the waiting entry and
find the series complete with no extra book; a series is never proposed a
second place for a title it already has, and a copy already held without an
ISBN is offered its ISBN rather than duplicated
(`aHeldCopyWithoutAnIsbnIsRecognised`, D-024).
**Related decisions.** D-004, D-006, D-012.
**History.** Identified while specifying F-024, 2026-10-04.

### AV-008 Joint credits counted as separate authors
**Severity:** Minor
**Description.** Treating a credit string as an author name makes
"Terry Pratchett" and "Terry Pratchett & Neil Gaiman" two different authors, so
every per-author total is wrong and filtering by an author misses their
collaborations. This is a realised defect in the source spreadsheet, carried in
if the importer does not split credits.
**Detection.** Implemented, 2026-10-05. `tests/test_import.cpp`,
`jointCreditsBecomeSeparateAuthors`, imports a joint and a solo credit and
asserts two authors, the collaboration counted for both, and cover order in
`v_book_display`.
**Related decisions.** D-007.
**History.** Observed in the seed data during cataloguing, 2026-08-30.

### AV-013 Re-importing an old file after correcting credits in the app
**Severity:** Major
**Description.** Import matches a book without an ISBN on its title and
first-billed author (SPEC.md §1.3). Correct a book's credits in the
application — an editor recorded as an author, a misspelt name — and an
older CSV still carrying the old credits no longer matches it: re-importing
that file inserts the book a second time. The import is idempotent only
against the file it came from.
**Detection.** Partly implemented, 2026-10-05: the one case that has
happened (BUG-003) was handled by correcting the converter and the database
together and re-importing the corrected file on a copy first, which reported
443 unchanged. No automatic guard exists; it would need either a match that
falls back to title alone when exactly one book has that title, or an
import that reports a likely duplicate rather than inserting it.
**Related decisions.** D-016, D-007.
**History.** Found while fixing BUG-003, 2026-10-05, before it could happen:
re-importing the uncorrected seed after the fix would have created three
duplicates.

---

## External dependencies

### AV-009 Provider quota exhausted mid-run
**Severity:** Major
**Description.** A provider stops answering partway through a batch: a quota
spent, a rate limit hit, a service down. Enriching 443 books takes well over a
thousand requests at up to three per book plus covers. A run that is not
resumable loses its progress, and one that treats a 429 as a failure marks
good books `failed` and moves on.
**Detection.** Partly implemented, 2026-10-06. `tests/test_metadata.cpp`:
`aQuotaReplyPausesAndRetries` (a 429 pauses the queue and the request is
retried), `persistentTroubleIsGivenUpOn`, `aMissingRecordIsNotRetried`,
`requestsAreSpacedApart`, `cancellingDropsWhatIsQueued`. At batch level
(D-023), `tests/test_enricher.cpp`, `anInterruptedBatchResumesWhereItLeftOff`:
a provider out of reach stops the run with the book in hand still
`unmatched`, and the next run asks nothing already answered.
**Related decisions.** D-008, D-019, D-020.
**History.** Identified while specifying Phase 3, 2026-10-04. On 2026-10-06
Google Books turned out to give a keyless quota of 0 — every request refused
with 429 — which is why D-019 put Open Library first.

### AV-010 Wrong edition or wrong book matched
**Severity:** Major
**Description.** A title-and-author search returns a plausible but wrong
result — a different edition, a same-titled book, an omnibus in place of a
single volume — and is accepted without review. The catalogue then holds
confident, wrong data, which is worse than an empty field because nothing
prompts a correction. Most acute for the 443 seeded books, none of which carry
an ISBN.
**Detection.** Partly implemented, 2026-10-06. Fetch metadata never writes
without the owner's choice, ISBN matches included; a search's candidates are
offered with none preselected and a note that any may be another edition,
and a search result never supplies publisher, page count or ISBN (SPEC.md
§3.5). `tests/test_detail_panel.cpp`, `searchedCandidatesWaitToBeChosen`;
`tests/test_enricher.cpp`, `fetchingFromThePanelWritesTheChoiceAndItsCover`;
`tests/test_domain.cpp`, `aSearchedCandidateGivesNoEditionFacts`. Not yet:
a report of books matched by search rather than ISBN. The batch run takes
only an ISBN's single answer whose title agrees (`titlesAgreeAcrossProviderNoise`,
`aBatchTakesOnlyWhatItMayAndQueuesTheRest`) and queues everything else for
review; a focused list no longer counts as a choice (BUG-004). The British Library's index returns
related editions for an ISBN; only records carrying the ISBN asked are kept
(`britishLibraryDropsRelatedEditions`, D-022).
**Related decisions.** D-008, D-012.
**History.** Identified while specifying the matching strategy in SPEC.md §3.3,
2026-10-04. A live instance, 2026-10-06: ISBN 9780000000002, chosen for a test
as surely unknown, is a real 1985 book in Open Library — any lookup by a
mistyped ISBN can return a confident, wrong answer.

---

## Export fidelity

### AV-011 Exported figures disagree with the application
**Severity:** Minor
**Description.** An export that recomputes series completeness in its own code
rather than reading `v_series_status` will drift from what the application
shows. Two sources of truth for one fact, in breach of the documentation
standard's Maintenance Rule 5.
**Detection.** Implemented, 2026-10-07. `tests/test_catalogue.cpp`,
`theWorkbookAgreesWithTheViews`, compares the workbook's Series status
sheet with `v_series_status` row for row — held, read, unread, status — and
checks the Authors sheet leaves editors out; the writer computes nothing
(D-025).
**Related decisions.** D-004.
**History.** Identified while specifying SPEC.md §5.1, 2026-10-04. The SQL
dump (F-021) cannot drift: it is the tables themselves, and each is checked
by restoring it.

---

## Catalogue files

### AV-014 Opening a database that is not a catalogue rewrites it
**Severity:** Critical
**Description.** Opening a file runs the migrations, which create Pinax's
schema in any database without a `schema_version` table. Pointed at some
other SQLite file — a browser's history, another program's data — through
Open, the command line or a recent list, Pinax would add a dozen tables to
it and switch it to WAL mode: damage done to someone else's file. A
catalogue from a newer Pinax opened by an older one fails its migrations
halfway, or worse.
**Detection.** Implemented, 2026-10-08. `db::inspect` reads a file
read-only before anything opens it, and the window and `main` refuse
anything that is not empty and not a catalogue, or is a newer version.
`tests/test_catalogue_files.cpp`, `notACatalogueIsNeverOpened`, opens
another program's database and a catalogue from schema version 99 and finds
both refused and unchanged; `inspectTellsACatalogueFromAnythingElse` covers
text files and empty ones. The command line exits 1 rather than open a
window.
**Related decisions.** D-002, D-027.
**History.** Identified while building F-026, 2026-10-08.

---

## Barcode capture

### AV-012 Wrong barcode read as an ISBN
**Severity:** Minor
**Description.** The webcam decodes something other than the book's ISBN — a
second-hand shop's price sticker, the 5-digit price add-on appended to the
main code, or a partial read of a blurred barcode — and the result is looked
up or, worse, assigned to an existing book during backfill. The wrong ISBN
then drives every later metadata fetch for that record.
**Detection.** Not implemented for the decoder (would require tests feeding
it a non-978/979 EAN-13, an EAN-13 with add-on, and a sequence of frames
that disagree, and asserting that none yields an accepted ISBN). The typed
path checks the check digit before any lookup (`aBadIsbnIsCaughtAtTheDoor`),
which the scanner will feed.
**Related decisions.** D-014, D-012.
**History.** Identified while planning F-025, 2026-10-05. The capture rules in
SPEC.md §6 exist for this reason; the confirmation step of D-012 is the last
line of defence.
