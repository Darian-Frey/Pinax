# Pinax

> **Status:** Active
> **Provenance:** Shane Hartley (author); Claude (primary auditor)
> **Last reviewed:** 2026-10-05
> **Why this status:** Scaffold, data model and design decisions complete;
> Phase 1 in progress — the application builds and opens an empty window.
> Toolchain fixed by D-001. `LICENSE` is deliberately absent — see Licence.

A Linux desktop catalogue for a personal physical library. Pinax tracks what is
on the shelf, what has been read and how often, ratings out of ten, and series
membership — including the volumes a series is known to contain but the shelf
does not. Synopses, cover art and genre categories are pulled from public
metadata providers. Everything lives in one local SQLite file and exports to
SQL, CSV and Excel.

Named for the *Pinakes*, Callimachus's catalogue of the Library of Alexandria.

---

## Quick start

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
build/src/pinax
```

The application is an empty three-panel window so far. The database is usable
on its own:

```sh
# create the catalogue
sqlite3 pinax.db < db/schema.sql

# confirm it applied
sqlite3 pinax.db "SELECT version, applied_at, note FROM schema_version;"

# series completeness, once populated
sqlite3 -header -column pinax.db "SELECT name, held, known, status FROM v_series_status ORDER BY name;"

# what is missing, fewest-needed first
sqlite3 -header -column pinax.db "SELECT series_name, position, title FROM v_missing_entries;"
```

Any client connecting to the database must issue `PRAGMA foreign_keys = ON;` on
each connection. SQLite does not persist that setting, and without it the
foreign keys in the schema are advisory only (AV-004).

---

## Build requirements

- **C++20** and **Qt6** (Widgets, Network, Sql) — fixed by D-001
- **SQLite 3.31** or later — `VACUUM INTO`, partial indexes
- **libxlsxwriter** for Excel export (F-022)
- CMake 3.21 or later

Packages, verified versions and troubleshooting are in [`BUILD.md`](BUILD.md).

---

## Project structure

```
pinax/
├── db/
│   └── schema.sql        schema version 1: tables, views, triggers, indexes
├── CMakeLists.txt
├── src/                  one directory per module, per ARCHITECTURE.md §2
│   ├── main.cpp
│   ├── app/              composition root, settings, main window shell
│   ├── domain/           plain value types, no Qt or SQL
│   ├── db/               connection, migrations, repositories
│   ├── metadata/         Google Books, Open Library, cover cache
│   ├── io/               CSV import, export, backup
│   └── ui/               rail, list, detail panel
├── tests/                Qt Test, run by ctest
├── design/
│   ├── Pinax UI.html     interactive mock-up, four screens
│   └── screens/          the same four screens as PNG
├── seed/                 the real catalogue; git-ignored, never committed
├── README.md
├── BUILD.md              requirements, build, test, troubleshooting
├── FEATURES.md           F-001 … F-025, MoSCoW priorities, acceptance criteria
├── ROADMAP.md            Phase 0 complete; Phases 1–5 planned
├── ARCHITECTURE.md       modules, data flow, invariants
├── DECISIONS.md          D-001 … D-014, append-only
├── SPEC.md               CSV format, ISBN validation, provider contracts, exports
├── ATTACK_VECTORS.md     AV-001 … AV-012, failure modes with detection
├── BUGS.md               empty; present so Rule 8 applies from commit one
├── IMPROVEMENTS.md       empty; same reason
├── CLAUDE.md             handoff: current state, invariants, pitfalls
└── CHANGELOG.md
```

---

## Data model in one paragraph

A **book** is one physical copy, carrying its own edition and condition notes
so that printing, jacket state and provenance survive any metadata fetch.
**Authors** are normalised, so a joint credit is two links rather than a
combined name, and counting books per author gives the same answer either way.
A **series entry** joins a series to a book — or to nothing, which is how a
volume known to exist but absent from the shelf is recorded. Series
completeness is therefore computed from held against known at query time and
cannot go stale; there is no stored status column, and the missing-volume list
is a view. Positions are stored as printed, since real ones include
`Broadcast 6.5`, `1-4` and `3a`, with a separate numeric key used only for
ordering.

---

## Documentation map

| Document | Contents |
|---|---|
| [`FEATURES.md`](FEATURES.md) | What it does, what it will not do, and what is merely a candidate |
| [`ROADMAP.md`](ROADMAP.md) | Phases, deliverables and acceptance criteria |
| [`ARCHITECTURE.md`](ARCHITECTURE.md) | Modules, data flow, invariants — descriptive only |
| [`DECISIONS.md`](DECISIONS.md) | Why it ended up this way, with reversal conditions |
| [`SPEC.md`](SPEC.md) | Formats and protocols: CSV, ISBN, providers, exports |
| [`ATTACK_VECTORS.md`](ATTACK_VECTORS.md) | How it can go wrong, and how that is detected |
| [`BUGS.md`](BUGS.md) · [`IMPROVEMENTS.md`](IMPROVEMENTS.md) | Realised defects, and candidate refactors |
| [`CLAUDE.md`](CLAUDE.md) | Handoff for AI development sessions |
| [`CHANGELOG.md`](CHANGELOG.md) | Version history with ID traceability |
| [`BUILD.md`](BUILD.md) | Requirements, build and test commands, troubleshooting |

---

## Licence

**None, deliberately.** The repository is private, and the omission is recorded
as D-013 with the condition that it is revisited before any public commit or
before anyone else is given access. Published without a licence, this code
would grant no rights to anyone.
