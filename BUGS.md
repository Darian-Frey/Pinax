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

### BUG-003 Editors imported as authors named "ed. …"

**Status:** open
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
**Reproduction.** Import the seed, then `SELECT name, sort_name FROM author
WHERE name LIKE 'ed.%';` — two rows.
**Notes.** Not fixed, per Maintenance Rule 8. Two routes, either sufficient:
(a) teach the converter that a leading `ed. ` means `Name (editor)` and
re-import — the credits are replaced, but the two `ed. …` author rows stay
behind with no books (see IMP-003); or (b) correct the three books in the
edit form, which F-002 now allows, with the same leftover rows. The
converter should learn the rule either way, in case the spreadsheet is ever
converted again.

## Fixed

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
