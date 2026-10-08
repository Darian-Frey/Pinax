# Bugs

Catalogue of bugs discovered during development. Per the project workflow,
bugs are **logged here when found, not silently fixed** (see the development
documentation standard, Maintenance Rule 8). The author decides whether to fix
immediately, defer, or leave alone.

This rule is load-bearing for AI-partner sessions, which otherwise default to
acting on a discovery rather than recording it. Log first; the decision to act
is the author's.

Status vocabulary: open | fixed | wontfix | deferred.
Severity vocabulary: low | medium | high.

Entry format — see ATTACK_VECTORS.md for the forward-looking counterpart; a
recurring pattern here may warrant a new AV entry, and an AV entry that escapes
detection becomes an entry here.

---

## Open

*None.*

## Fixed

### BUG-006 Restore from Backup was offered only with a catalogue open

**Status:** fixed (2026-10-08)
**Found:** 2026-10-08 (by the owner, trying F-027 before it was committed)
**Location:** `src/app/main_window.cpp`, `restoreChosen` and `updateActions`
**Severity:** medium
**Description.** Restore from Backup replaced the open catalogue, so with no
catalogue open it was greyed out. The owner backed up, closed the catalogue
— a natural first step before replacing it — and then could not restore.
**Reproduction (was).** Open a catalogue, Back up, Close Catalogue: File ▸
Restore from Backup is disabled.
**Fix.** Restore is offered whenever the panel is not busy. With a
catalogue open it replaces that one, as before; with none open, the save
dialogue asks where to restore, suggesting the catalogue last open. A
catalogue at that path is backed up first; a file there that is not a
catalogue is refused (AV-014); a new path simply receives the catalogue.
Either way it then opens. `tests/test_catalogue_files.cpp`,
`restoringWithNothingOpen`, follows the owner's steps. Fixed in the same
change as F-027, which had not been committed; logged for the owner's sight.

### BUG-005 A book's series cannot be edited with the book, and no series can be created

**Status:** fixed (2026-10-07)
**Found:** 2026-10-07 (by the owner, editing a book)
**Location:** `src/ui/book_editor.cpp` (no series fields); `src/app/catalogue.cpp`, `save(const BookEdit&)` (writes no series); nowhere in the application creates a `series` row
**Severity:** medium
**Description.** The edit form (F2) shows title, credits, edition facts,
notes and synopsis, but not the series the book belongs to or its position
in them. A book can be placed in a series only from the series' own page —
add a volume, then Mark as owned — so the owner must know to go there
first, and cannot move a book from one series to another, or correct its
position, from the book itself. And no part of the application creates a
series: series come only from `--import-series` and the importer, so a
series begun since the import cannot be recorded at all. F-008's criteria
are met on the series page, which is why it was marked Complete; the
everyday path, from the book, was never built.
**Reproduction.** Select any book, press F2: no series section. Look for a
way to start a series not already in the catalogue: there is none.
**Fix.** As proposed, by the owner's decision. A Series section in the edit form, one row per series the
book is in — series name (chosen from the catalogue's series, or typed: a
new name creates the series), position as printed, sort number — with
Remove and "Add to a series". Saved with the book, in its transaction:
- joining a series where a missing volume waits at the same position, or
  with the same title, fills that entry rather than adding a second
  (AV-007);
- leaving a series removes the book's entry, as Remove from series does on
  the series' page; the series stops counting it;
- a sort number left empty is derived from the position by the importer's
  rule, the only one (AV-006);
- an omnibus may still share a position with its volumes.
`BookEdit` carries the rows as `SeriesPlacement`s — nullopt on every path
but the form, which leaves series alone — and `Catalogue::placeInSeries`
applies them inside `save`'s transaction; an existing series is matched by
name case-blind, a new one created. Tests: `aBooksSeriesAreEditedWithTheBook`
and, through the form, `theFormPlacesABookInASeries`.
**Notes.** The series page's entry editor stays as it is, for volumes not
owned.

### BUG-004 Focusing the candidate list chose its first search result

**Status:** fixed (2026-10-06)
**Found:** 2026-10-06 (Phase 3 step 5, a live review screen against a catalogue copy)
**Location:** `src/ui/candidate_view.cpp`, `CandidateView::offer`; `src/ui/detail_panel.cpp`, `offerCandidates`
**Severity:** medium
**Description.** Candidates found by title and author are offered with
none chosen (AV-010). But the panel then gave the list keyboard focus, and a
list given focus makes its first row current; Use this followed the current
row, so it was enabled and Enter would take the first result — the
preselection AV-010 forbids. Introduced with Fetch metadata (step 4). The
tests did not see it: an offscreen window that is never made active never
really takes focus.
**Reproduction (was).** In a shown, active window, fetch a book without an
ISBN: the first candidate is highlighted and Use this is enabled.
**Fix.** Use this follows the selection, which focus never changes, rather
than the current row; a search's candidates leave the focus on Cancel (or
Skip, when reviewing), and only an ISBN's answer focuses the list.
`tests/test_detail_panel.cpp`, `searchedCandidatesWaitToBeChosen`, now runs
in an active window and tabs into the list. Found and fixed in the same
step because the review queue built there uses the same view; logged here
for the owner's sight.

### BUG-003 Editors imported as authors named "ed. …"

**Status:** fixed (2026-10-05)
**Found:** 2026-10-05 (finishing Phase 1, reviewing the list against the real catalogue)
**Location:** the seed data — `seed/library.csv`, made by `seed/convert_catalogue.py`, both git-ignored; three books in the owner's `pinax.db`
**Severity:** low
**Description.** The source spreadsheet marks an anthology's editor by
prefixing "ed." to the name. The converter passed those cells through as
written, so the importer created two authors named `ed. Mike Ashley` and
`ed. Jonathan Strahan`, filed as `Ashley, ed. Mike` and `Strahan, ed.
Jonathan`, credited with the role `author`. Three books are affected: *The
Mammoth Book of Mindblowing SF*, *Lost Mars* and *Engineering Infinity*.
They sort under the wrong names, count as authored rather than edited, and
would not merge with the same people credited correctly elsewhere (AV-008's
failure, by a different route).
**Reproduction (was).** Import the seed, then `SELECT name, sort_name FROM author
WHERE name LIKE 'ed.%';` — two rows.
**Fix.** Converter and catalogue corrected together, because either alone
would have caused AV-013: re-importing a corrected CSV would no longer match
the three books under their old credits and would have inserted them again.
In order: the converter learned that a leading `ed. ` means `Name (editor)`
and `seed/library.csv` was regenerated (three lines changed); the owner's
database was backed up with `VACUUM INTO` beside itself
(`pinax-2026-10-05-before-bug003.db`); the three books were re-credited
through `Catalogue::save`, the edit form's own path; the two `ed. …` author
rows, then credited by nothing, were deleted; and the corrected CSV,
re-imported on a copy first, reported 443 unchanged. All of it ran on a copy
before it ran on the real file.
**Notes.** Re-importing the corrected file alone, the first route
considered, would have duplicated the three books; that is AV-013. The list
now shows these books with an empty Author column, since they have an
editor and no author: IMP-004.

### BUG-002 `v_book_display` joins credits and series in arbitrary order

**Status:** fixed (2026-10-05)
**Found:** 2026-10-05 (Phase 1 step 3, while extending the view for the list's sort keys)
**Location:** `db/schema.sql`, view `v_book_display`, columns `authors`, `series`, `positions` (schema version 1)
**Severity:** medium
**Description.** The `authors` column was built as
`SELECT group_concat(a.name, ' & ') … ORDER BY ba.ordinal`. In SQLite an
`ORDER BY` beside an aggregate orders the single result row, not the values
the aggregate consumes, so names were joined in whatever order the index scan
produced. F-002's "cover order is preserved" did not hold in the one place
the list reads it. `series` and `positions` were joined the same way with no
order at all, so for a book in several series nothing tied the n-th position
to the n-th series, and `group_concat` skipping NULL positions could shift
them out of step.
**Reproduction (was).** Insert a book with two authors, the first-billed
(`ordinal` 0) having the higher `author.id` and inserted second. `SELECT
authors FROM v_book_display` returned 'Jerry Pournelle & Larry Niven' for
*The Mote in God's Eye*.
**Fix.** Schema version 2 (`db/migrations/002_book_display_sort_keys.sql`)
rebuilds the view with each multi-valued column joined from an ordered inner
subquery: credits by `ordinal` then filing name, series by name then entry
id, and positions in step with series using `''` where none is printed. The
owner approved fixing it in the same migration that added the list's sort
keys rather than as a separate change. `tests/test_db.cpp`:
`authorsJoinInCoverOrder`, `severalSeriesJoinInStableOrder`.
**Notes.** `group_concat(x, sep ORDER BY y)` would say this directly but needs
SQLite 3.44; the ordered-subquery form works from the 3.31 floor. Any future
view that joins values must use the same pattern.

### BUG-001 Deleting a book keeps its series entry; F-001 says it should go

**Status:** fixed (2026-10-05)
**Found:** 2026-10-05 (Phase 1 step 2, writing the deletion test for `BookRepository`)
**Location:** `db/schema.sql` (`series_entry.book_id ... ON DELETE SET NULL`); FEATURES.md F-001; ROADMAP.md Phase 1 acceptance
**Severity:** low
**Description.** F-001 says deleting a book "removes its credits, series
entries and genre links, and leaves no orphan rows", and the Phase 1
acceptance repeats "no orphan credits or series entries". The schema instead
sets `series_entry.book_id` to NULL, which turns the entry into a
known-but-unowned volume (D-006). Credits and genre links do cascade as
specified. The two behaviours are each defensible: if the book has left the
shelf, the series still contains that volume and should now show it missing;
if the record was a mistake, the leftover entry is a ghost gap. The documents
and the schema disagree about which is meant.
**Reproduction (was).** Create a book, attach it to a series through
`series_entry`, delete the book with foreign keys on: the `series_entry` row
survives with `book_id IS NULL` and the series reports Incomplete.
`tests/test_db.cpp`, `deletingABookLeavesNoOrphanLinks`, asserts the current
schema behaviour.
**Fix.** Resolved in the documents, not the schema: the owner chose to keep the
entry as a missing volume. Deleting a book means it has left the shelf, and
the series should then show the gap. F-001's acceptance and the Phase 1
acceptance now say so; `ON DELETE SET NULL` stays, and no migration was
needed. A book entered in error leaves a gap that is removed from the series
itself. `deletingABookLeavesNoOrphanLinks` asserts the agreed behaviour.
**Notes.** The alternative was `ON DELETE CASCADE` through a version 2
migration, so that deleting a book also deletes its series entries; rejected
because selling or losing a volume would then silently shrink the series. A
UI that asks whether the volume left the shelf or was entered in error remains
possible later; it would need a DECISIONS entry and would have to work without
a modal dialogue (D-011).

## Won't Fix

*None.*

## Deferred

*None.*
