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
