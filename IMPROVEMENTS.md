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
