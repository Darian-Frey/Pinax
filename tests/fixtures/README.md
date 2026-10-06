# Test fixtures

Recorded or constructed inputs for the tests. Nothing here comes from the
owner's catalogue.

| File | Origin |
|---|---|
| `schema_v1.sql` | `db/schema.sql` frozen at version 1. Never edit. |
| `open_library/isbn_9780316005388.json` | Recorded 2026-10-06: Books API, `jscmd=data`, for *Consider Phlebas*. |
| `open_library/edition_9780316005388.json` | Recorded 2026-10-06: `/isbn/9780316005388.json` (edition OL9759601M); its description is the `{type, value}` shape. |
| `open_library/work_OL8368432W.json` | Recorded 2026-10-06: the work; its description is the plain-string shape. |
| `open_library/search_consider_phlebas.json` | Recorded 2026-10-06: `search.json` by title and author. |
| `open_library/isbn_unknown.json` | Recorded 2026-10-06: the Books API for an ISBN Open Library does not hold (`{}`). |
| `google_books/quota_exceeded_keyless.json` | Recorded 2026-10-06: Google's answer to any request made without an API key (HTTP 429, daily quota 0). The finding behind D-019. |
| `google_books/volumes_documented_shape.json` | **Constructed**, not recorded: no API key was available. Follows the documented `volumes` response shape; ids and the description are invented. Replace with a recording once a key exists. |

ISBN 9780000000002, once tried as an "unknown" ISBN, is a real 1985 book in
Open Library — a live instance of AV-010 — and is deliberately not used.
