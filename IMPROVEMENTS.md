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

## Applied

### IMP-010 Let a finish date be entered for books read before Pinax

**Status:** applied (2026-10-08)
**Found:** 2026-10-07 (F-016, adding the Finished column)
**Location:** `src/ui/book_editor.cpp` (no date field); `src/io/csv_importer.cpp` (no date column); SPEC.md §1
**Effort:** small
**Description.** `date_finished` is written only by `trg_book_finished`, when
a book moves into read in the application. The 176 books imported as read
have none, and the edit form offers no way to enter one, so sorting by date
finished orders only what is read from now on; the backlog sorts last, as
if never finished.
**Proposal.** A "Finished" date field in the edit form, shown for a read
book, validated as an ISO 8601 date and written as is; and an optional
`date_finished` column in the import CSV (SPEC.md §1), so a spreadsheet of
remembered dates can be loaded in one go.
**Trade-offs.** A hand-entered date and the trigger both write the column:
marking a book read again overwrites the owner's date with today's, which
is right for a re-read but surprising after a correction. Most past dates
are not remembered, so many fields would stay empty or be guessed — a
guessed date sorts as confidently as a true one. Leaving it means the
column only describes reading done with Pinax.
**Notes.** `date_started` has the same gap and the same trigger-free status;
settle both together.
**Applied.** As proposed, by the owner's decision, for both dates as the
entry's note asked. The edit form gains Started and Finished, each a day, a
month or a year (`domain::isPartialIsoDate`, which the Acquired field now
shares); the import format gains optional `date_started` and
`date_finished` columns, checked the same way and reported by line; the CSV
export writes them, so a round trip keeps them. The trade-off the entry
named is handled: a finish date the owner *changed* in the edit that also
marks the book read wins over the trigger's stamp of today; one merely
carried along does not, so a re-read through the form is dated the day it
ends. A file's `date_finished` likewise wins over the stamp. Guessed dates
still sort as confidently as true ones; that is the owner's to weigh.
`tests/test_domain.cpp`, `datesAreAsPreciseAsRemembered`;
`tests/test_catalogue.cpp`, `aFinishDateTypedIsKeptButAReReadIsStamped`;
`tests/test_import.cpp`, `datesComeInAndGoOutWithTheBooks`.


### IMP-009 Treat genre names that differ only in case as one genre

**Status:** applied (2026-10-08)
**Found:** 2026-10-06 (adding the British Library, D-022)
**Location:** `db/schema.sql`, table `genre` (`name TEXT NOT NULL UNIQUE`); `src/db/genre_repository.cpp`, `findOrCreate`
**Effort:** small
**Description.** The British Library heads a subject "Science fiction";
Open Library and Google say "Science Fiction". `genre.name` is unique as
written, so one fetch of *Titan* links the book to both, and the genre
filter (F-017) would list two entries for one idea.
**Proposal.** Match genres case-insensitively in `findOrCreate` (`WHERE name =
:name COLLATE NOCASE`), keeping the first spelling stored; in the next
schema change, declare the column `COLLATE NOCASE` so the unique index
agrees. The names stay verbatim apart from case (D-009).
**Trade-offs.** Whichever spelling arrives first is the one shown, so the
display depends on fetch order. A migration that merges existing
case-variants must repoint `book_genre` rows, which is more than a column
change. Leaving it means duplicate genres to tidy by hand later.
**Notes.** Only exact case variants; "Fiction, science fiction, general" and
"Science fiction" remain different genres, as D-009 intends.
**Applied.** As proposed, by the owner's decision, in schema version 6
rather than by declaring the column `COLLATE NOCASE`, which would have meant
rebuilding `genre` and the foreign key that points at it. The migration
(`006_genre_name_nocase.sql`) merges genres that differ only in case into
the one stored first — each book keeping a single link, marked the owner's
if either link was — and then adds a case-blind unique index,
`idx_genre_name_nocase`, so they cannot part again. `findOrCreate` matches
case-blind, so the spelling stored first stands. On the owner's catalogue
one book's "Science Fiction" became "Science fiction", the spelling five
books already used. SQLite's NOCASE folds ASCII letters only, which covers
the genres providers send. `tests/test_db.cpp`,
`version6MergesGenresThatDifferOnlyInCase`; the migration test now runs
version 1 to 6.


### IMP-008 Leave Open Library's library-service subjects out of genres

**Status:** applied (2026-10-08)
**Found:** 2026-10-06 (Phase 3 step 4, the first real fetch into a catalogue copy)
**Location:** `src/metadata/open_library.cpp`, `parseBooksApi` and `parseSearch`
**Effort:** small
**Description.** Open Library's subjects mix genres with tags about the
library copy: *Consider Phlebas* by ISBN brings "Fiction", "Science Fiction"
and "Imaginary wars and battles", but also "Accessible book", "Protected
DAISY", "OverDrive" and "Long now manual for civilization". Stored verbatim
(D-009, D-019), these become genres in the rail's future filter (F-017),
where every book Open Library lends out would share "Accessible book". A
search result's `subject` list can run to dozens.
**Proposal.** Drop a short, fixed list of service tags — "Accessible book",
"Protected DAISY", "In library", "Lending library", "OverDrive", "Large type
books", "Long now manual for civilization" and the like — and keep at most
the first ten subjects from a search. Everything kept is still verbatim.
**Trade-offs.** A blocklist is a small taxonomy decision, which D-009 set
out to avoid, and it will lag Open Library's own tags. Leaving it means
cleaning genres by hand later, once a genre editor exists, and a filter
cluttered meanwhile. Filtering at display time instead keeps the data
verbatim but hides the problem rather than solving it.
**Notes.** The owner's first fetch, *Surface Detail* by search, brought five
clean subjects; the noise shows on ISBN lookups more than on searches.
**Applied.** As proposed, by the owner's decision, with one addition the
owner's catalogue called for. `domain::isServiceSubject` is the one rule: a
short fixed list (Accessible book, Protected DAISY, In library, Lending
library, OverDrive, Large type and Large print books, Long Now Manual for
Civilization, New York Times bestseller and reviewed, Internet Archive
Wishlist, Open Library Staff Picks), compared case-blind, plus Open
Library's machine tags — `nyt:…=…`, `award:…=…` — which the catalogue held
and the entry had not foreseen. `planEnrichment` never writes such a genre,
from any provider; Open Library's search keeps at most its first ten real
subjects. The addition: links already stored are removed when a catalogue
opens (`GenreRepository::removeServiceSubjects`), never the owner's own,
with any genre left unused — on the owner's catalogue, 7 of 99 links and 7
of 57 genres. Real subjects however odd ("Sheriffs", "Captain Frey
(Fictitious character)") and headings in other languages ("Roman") stay, as
D-009 intends. `tests/test_domain.cpp`, `lendingAndListTagsAreNotGenres`;
`tests/test_metadata.cpp`, `aSearchedWorkGivesOnlyTypicalFigures`;
`tests/test_catalogue.cpp`, `openingTidiesLendingTagsAwayButNotTheOwners`.


### IMP-007 Fall back to a free-text Google query when a qualified one finds nothing

**Status:** applied (2026-10-08)
**Found:** 2026-10-06 (testing the owner's new Google Books API key)
**Location:** `src/metadata/google_books.cpp`, `isbnUrl`, `searchUrl`
**Effort:** small
**Description.** With a working key, Google Books answered every
field-qualified query with no results — `q=isbn:9780316005388`,
`q=intitle:Dune`, even *Dune*'s own ISBN — and sometimes "Service temporarily
unavailable", while `q=Consider Phlebas Banks` found 179. The client uses
only qualified queries (SPEC.md §3.2), so as things stand Google adds
nothing even with a key.
**Proposal.** When a qualified query returns no items, ask once more in free
text: the ISBN alone, or title and author words. Keep only results whose
ISBN matches, for an ISBN lookup; a title-and-author result is queued for
confirmation as always (AV-010).
**Trade-offs.** Two requests where one should do, against the daily quota.
Free text is looser: "Consider Phlebas Banks" also returns a book *about*
the Culture novels, so the ISBN filter for lookups is essential and title
results lean harder on the owner's confirmation. If the qualified search is
a fault of a newly created key or a passing Google problem, the fallback is
dead weight once it clears — worth retrying the qualified form in a few
days before building anything.
**Notes.** Google is the second opinion behind Open Library (D-019), so
nothing is blocked meanwhile.
Still so on 2026-10-06, later: eight ISBNs from the owner's shelf, asked as
`isbn:`, found nothing on Google, while Open Library knew six. Asked in free
text, `9780575078017` (*Sunstorm*) returned one item — *The British National
Bibliography*, whose scanned pages contain the number — so the ISBN filter
the proposal calls essential is borne out.
**Applied.** As proposed, by the owner's decision, after a retry on
2026-10-08 found the qualified queries still answering nothing, or 503.
`GoogleBooksClient::lookupIsbn` and `search` ask the qualified way first
and, only when that finds nothing without error, ask once more in free text
(`freeTextUrl`): an ISBN's answers are kept only if a volume carries that
ISBN, as ISBN-13 or ISBN-10; a search's only if the title agrees
(`domain::titlesAgree`) and an author is shared (`domain::shareAnAuthor`),
and the owner still chooses (AV-010). Live, the searches came back to life —
*Titan* by Stephen Baxter, both editions of *Consider Phlebas*, the study of
the Culture novels filtered out — while the ISBN fallback found nothing for
a book Google holds: free text does not index ISBNs well, and that request,
made only after a miss, is kept for the cases where it does.
`tests/test_metadata.cpp`: `googleSearchFallsBackToFreeText`,
`googleIsbnFallsBackToFreeTextKeepingOnlyThatIsbn`,
`googleAnswersAreNotAskedTwice`.


### IMP-005 Summarise placeholder volumes in the detail panel

**Status:** applied (2026-10-06)
**Found:** 2026-10-05 (Phase 2 step 1, viewing Discworld after the series import)
**Location:** `src/ui/book_view.cpp`, `missingText`
**Effort:** trivial
**Description.** The panel lists up to three missing volumes by name, then
"and N more". For a series whose gaps are placeholders, that reads "Missing
14: Unidentified volume 1, Unidentified volume 2, Unidentified volume 3 and
11 more" — three names that say nothing.
**Proposal.** When every missing volume is unidentified, say so: "Missing 14,
not yet identified". When some are named, list the named ones and count the
rest: "Missing Sea of Sorrows, River of Pain and 3 unidentified".
**Trade-offs.** The panel would need to recognise a placeholder, and the only
mark one carries is its title. Either the title pattern becomes a convention
the code relies on, or entries gain a flag — a schema change for a display
nicety. A third route is to leave it until the series view (Phase 2 step 3)
shows placeholders in place, where the repetition matters less.
**Applied.** As proposed, by the owner's decision, recognising a
placeholder by its title: `domain::isPlaceholderTitle` is the one place the
convention is read, and the two copies of the check already in the code (the
attach search and the new-book prefill) now use it. Discworld reads "Missing
14, not yet identified" on a book and "14 volumes, not yet identified" on the
series; a mix names the named and counts the rest. No schema change.
`tests/test_detail_panel.cpp`, `placeholdersAreCountedNotNamed`;
`tests/test_series_page.cpp`, `panelCountsPlaceholders`.
**Notes.** Placeholders were the owner's choice for the seed (D-018).

### IMP-004 Show editors in the list when a book has no author

**Status:** applied (2026-10-06)
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
**Applied.** As proposed, by the owner's decision: schema version 3
(`db/migrations/003_book_display_editors.sql`). A book with no author credit
shows its editors — "Mike Ashley (ed.)", "A & B (eds.)" — and files under the
first. The trade-off is recorded where it will be met: the view's comment and
F-017 both say per-author counts come from role 'author' in `book_author`,
never from this view. `tests/test_db.cpp`,
`editorsStandInWhenThereIsNoAuthor`; the migration test now runs v1 to v3.
In the owner's catalogue no book is left with a blank Author cell.
**Notes.** The detail panel already shows every credit via the edit form; only
the list and its sort are affected.

### IMP-002 Keep unsaved edits when the selection moves

**Status:** applied (2026-10-05)
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
**Applied.** As proposed, by the owner's decision. `DetailPanel::isBusy()`
is true while editing or confirming a deletion; `MainWindow::lockWhileBusy`
disables the list and Add a book and says why in the status bar, and
restores both when the panel lets go. A disabled list takes neither clicks
nor its single-key actions. Tests: `listStandsStillWhileEditing`,
`listStandsStillWhileConfirmingADelete`.
**Notes.** The bulk editor (multi-selection) will meet the same question;
settle it once for both.

### IMP-003 Authors left with no books stay in the author table

**Status:** applied (2026-10-05)
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
**Applied.** As proposed, by the owner's decision, and extended to the
importer: `AuthorRepository::removeUncredited()` runs in the same transaction
after `Catalogue::save` changes credits, after `Catalogue::remove`, and at the
end of an import run, so a corrected re-import leaves no stale spelling.
Authors with notes are kept. The owner's catalogue had none to remove when
this landed. Tests: `creditedAwayAuthorsAreRemoved`,
`deletingAnAuthorsLastBookRemovesThem`, `correctedReimportLeavesNoStragglers`.
**Notes.** Settle before F-017's author filter is built.

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

### IMP-006 Keep placeholder-only gaps out of "one volume short"

**Status:** deferred (2026-10-06)
**Found:** 2026-10-06 (Phase 2 step 5, viewing the shopping list against the owner's catalogue)
**Location:** `db/schema.sql` (`v_series_status`, `known - held = 1`); `src/db/series_repository.cpp`, `libraryTotals`; `src/app/catalogue.cpp`, `missingVolumes`
**Effort:** small
**Description.** "One volume short" counts series lacking exactly one
entry. Three of the 28 in the owner's catalogue — Darkover, Exodus, Known
Space — lack exactly one *placeholder*, "Later volumes — unidentified",
which stands for an unknown number of books (D-018). They are not one
purchase from complete, yet they sit in the shopping list beside The
Culture's Consider Phlebas.
**Proposal.** Leave those series out of "one volume short" — the rail's
count, the panel's "Series one volume short" and the list — while keeping
them under "Missing volumes". The test is the placeholder title, through
`domain::isPlaceholderTitle`.
**Trade-offs.** The count lives in SQL (`libraryTotals`, from the views);
the placeholder test lives in C++. Either the SQL learns the title
convention, a second place reading it (against IMP-005's one-place rule),
or the count moves out of SQL into C++ over `missingEverywhere()`, which
recomputes a derived figure outside the views (ARCHITECTURE.md §2, AV-011).
A third way is a column marking placeholders, a schema change D-018 chose
not to make. Leaving it as it is keeps every count derived in one place, at
the cost of three misleading rows.
**Deferred.** By the owner, for now. The three rows are recognisable — the
title reads "Later volumes — unidentified", greyed — and the issue goes away
as those placeholders are named or removed with Edit entry. If placeholders
prove long-lived, a column marking them is the fix to revisit, alongside the
next schema change.
**Notes.** The spreadsheet's own "one book completes" list had 14 entries;
the mechanical count of 28 differs mostly for other reasons — single-volume
holdings of two-book series count too — which is correct by the
definition and not part of this entry.
