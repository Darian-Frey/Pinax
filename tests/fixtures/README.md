# Test fixtures

Recorded or constructed inputs for the tests. Nothing here comes from the
owner's catalogue.

| File | Origin |
|---|---|
| `schema_v1.sql` | `db/schema.sql` frozen at version 1. Never edit. |
| `open_library/isbn_9780316005388.json` | Recorded 2026-10-06: Books API, `jscmd=data`, for *Consider Phlebas*. |
| `open_library/edition_9780316005388.json` | Recorded 2026-10-06: `/isbn/9780316005388.json` (edition OL9759601M); its description is the `{type, value}` shape. |
| `open_library/work_OL8368432W.json` | Recorded 2026-10-06: the work; its description is the plain-string shape. |
| `open_library/search_consider_phlebas.json` | Re-recorded 2026-10-08: `search.json` by title and author, with `cover_edition_key` among the fields (D-029). One work; its page count is the median of its editions, 471. |
| `open_library/edition_OL9041460M.json` | Recorded 2026-10-08: the edition behind that search's cover — the German *Bedenke Phlebas* (Heyne, 762 pages), which is why a work's cover is no proof of the owner's edition. |
| `open_library/isbn_unknown.json` | Recorded 2026-10-06: the Books API for an ISBN Open Library does not hold (`{}`). |
| `british_library/isbn_9780356521633.xml` | Recorded 2026-10-06: SRU `alma.isbn=` for *Consider Phlebas*, Orbit 2023 — a reprint whose 008 gives the 1987 original. |
| `british_library/isbn_9780708837078.xml` | Recorded 2026-10-06: the same query for the 1988 Futura paperback, whose record gives only its ISBN-10. |
| `british_library/isbn_9780316005388.xml` | Recorded 2026-10-06: the US Orbit ISBN, which the British Library does not hold — no records. |
| `google_books/quota_exceeded_keyless.json` | Recorded 2026-10-06: Google's answer to any request made without an API key (HTTP 429, daily quota 0). The finding behind D-019. |
| `google_books/freetext_consider_phlebas.json` | Recorded 2026-10-06 with the owner's API key (the key is not in the file): a free-text search, `q=Consider Phlebas Banks`, three results — two editions of the book, one about it. |
| `google_books/isbn_no_match.json` | Recorded 2026-10-06 with the key: `q=isbn:9780316005388`, which found nothing although Google holds the book (see SPEC.md §3.2, IMP-007). |

No fixture may contain an API key. Record with the key in the URL only, and
check the saved file with `grep` before committing.

ISBN 9780000000002, once tried as an "unknown" ISBN, is a real 1985 book in
Open Library — a live instance of AV-010 — and is deliberately not used.
