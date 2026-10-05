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

### BUG-001 Deleting a book keeps its series entry; F-001 says it should go

**Status:** open
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
**Reproduction.** Create a book, attach it to a series through
`series_entry`, delete the book with foreign keys on: the `series_entry` row
survives with `book_id IS NULL` and the series reports Incomplete.
`tests/test_db.cpp`, `deletingABookLeavesNoOrphanLinks`, asserts the current
schema behaviour.
**Notes.** Not fixed, per Maintenance Rule 8. Resolving it means choosing
one: amend F-001 and the Phase 1 acceptance to say the entry is kept as
unowned (no code change), or change the schema to `ON DELETE CASCADE` (a
version 2 migration). A middle way — the UI asking whether the volume left the
shelf or was entered in error — would need a DECISIONS entry, and would have
to work without a modal dialogue (D-011).

## Fixed

*None.*

## Won't Fix

*None.*

## Deferred

*None.*
