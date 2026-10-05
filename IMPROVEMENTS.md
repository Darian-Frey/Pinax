# Improvements

Catalogue of code-quality improvements, refactors, and architectural changes
proposed during development. Per the project workflow, improvements are
**logged here when noticed, not silently applied** (see the development
documentation standard, Maintenance Rule 8). The author decides whether to
apply, defer, or decline.

This is the dual of BUGS.md: bugs are things that are broken, improvements are
things that work but could be better.

As with BUGS.md, this rule exists mainly to hold AI-partner sessions to logging
rather than acting. An improvement applied in passing, during work on something
else, is an unreviewed change.

Status vocabulary: suggested | applied | declined | deferred.
Effort vocabulary: trivial | small | medium | large.

**Trade-offs are not optional.** An entry without a `Trade-offs:` field is a
feature request, not an improvement candidate, and should be rejected at review.

---

## Suggested

### IMP-002 Keep unsaved edits when the selection moves

**Status:** suggested
**Found:** 2026-10-05 (Phase 1 step 5, building the detail panel's edit state)
**Location:** `src/app/main_window.cpp`, `MainWindow::showSelection`; `src/ui/detail_panel.cpp`
**Effort:** small
**Description.** While the panel is in its edit state, selecting another row
in the list — a stray click, an arrow key — shows the new selection and
throws away whatever was typed into the form, without a word. Nothing reaches
the database, so no data is corrupted, but an edit to a condition note or a
synopsis can vanish.
**Proposal.** While editing, ignore selection changes in the panel and mark
the list as inactive (dimmed, with a status-bar line "Save or cancel the edit
first"), restoring the selection to the edited book. Save or Esc ends the
edit and the list responds again. No dialogue, so D-011 holds.
**Trade-offs.** The list stops responding to clicks while a form is open,
which may feel stuck to someone who did not notice they were editing. The
alternative — saving automatically on leaving — writes changes the owner may
have meant to abandon, and turns a validation failure into a puzzle.
**Notes.** The bulk editor (multi-selection) will meet the same question;
settle it once for both.

### IMP-003 Authors left with no books stay in the author table

**Status:** suggested
**Found:** 2026-10-05 (finishing Phase 1, while making credits editable)
**Location:** `src/app/catalogue.cpp`, `Catalogue::save(const BookEdit&)` and `Catalogue::remove`
**Effort:** small
**Description.** Editing a credit to a different name, or deleting a book,
can leave an author row that no book credits any more — a misspelling
corrected, an "ed. …" name fixed (BUG-003), the last book by someone
deleted. Nothing is wrong in the data, but the author filter in the rail
(F-017) would list people with no books, and a later import of the old
spelling would quietly reattach to the stale row.
**Proposal.** After a credit change or a deletion, in the same transaction,
delete authors that have no `book_author` rows and no `notes`. One
`AuthorRepository::removeUncredited()` and a test that edits a credit away
and finds the author gone.
**Trade-offs.** An author kept deliberately — say, one whose books are lent
out and deleted for now — vanishes with their sort name; the `notes`
exception protects only those with notes. The alternative is to leave rows
and have the rail hide authors with a count of nought, which keeps history
but leaves the stale-spelling trap.
**Notes.** Settle before F-017's author filter is built.

### IMP-004 Show editors in the list when a book has no author

**Status:** suggested
**Found:** 2026-10-05 (fixing BUG-003)
**Location:** `db/schema.sql`, view `v_book_display`, columns `authors` and `author_sort`
**Effort:** small
**Description.** The list's Author column and its sort key come from credits
with the role `author` only. An anthology credited only to its editor — the
three BUG-003 corrected — shows an empty Author cell and sorts after every
authored book, though the spreadsheet showed the editor there and a reader
looks for an anthology under its editor.
**Proposal.** Schema version 3: when a book has no `author` credit, fall back
to its editors for `authors` (shown as "Mike Ashley (ed.)") and `author_sort`
("Ashley, Mike"). One migration file, the frozen-fixture test extended to
version 2, and a test in `test_db`.
**Trade-offs.** "Author" then means "author, or editor when there is none",
which a per-author count (F-017's filter) must not inherit — counts should
stay on the `author` role, or Mike Ashley would appear to have written two
books. Translators and illustrators are not proposed as fallbacks.
**Notes.** The detail panel already shows every credit via the edit form; only
the list and its sort are affected.

## Applied

### IMP-001 Order an author's books by series before title

**Status:** applied (2026-10-05)
**Found:** 2026-10-05 (Phase 1 step 4, viewing the imported seed catalogue)
**Location:** `src/ui/book_sort_proxy.cpp`, `compareBooks`, `AuthorColumn`
**Effort:** trivial
**Description.** Sorting by author falls back to sort title within an author,
so an author's series interleave: Douglas Adams reads Dirk Gently 1,
Hitchhiker's Guide 1, Hitchhiker's Guide 3, Dirk Gently 2, and so on. With
the seed's heavy series membership most authors show the same shuffle.
**Proposal.** In the author case, after `authorSort`, compare `seriesSort`
then `seriesSortPosition` (missing last, as elsewhere), then fall through to
sort title. Standalones would follow the author's series. One test in
`tests/test_book_list.cpp` alongside `authorSortsByFilingName`.
**Trade-offs.** A reader scanning one author for a title they half remember
loses the alphabetical run; standalones move after series rather than among
them. Search (F-019) makes the first less important once it exists. The mock-up
shows no author with books in two series, so it does not settle which is
intended.
**Applied.** As proposed, by the owner's decision. `compareSeries` in
`src/ui/book_sort_proxy.cpp` is shared by the author and series columns;
`tests/test_book_list.cpp`, `authorKeepsTheirSeriesTogether`, covers it.
**Notes.** Grouping by series (F-018) would make the question moot for the
grouped view, but not for the flat list.

## Declined

*None.*

## Deferred

*None.*
