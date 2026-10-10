# Pinax for developers

How Pinax is built and laid out, and where its decisions are written down.
For using it, see [USER_GUIDE.md](USER_GUIDE.md).

---

## Build requirements

- **C++20** and **Qt6** (Widgets, Network), fixed by D-001
- **SQLite 3.31** or later, used directly rather than through Qt SQL (D-015),
  for `VACUUM INTO` and partial indexes
- **libxlsxwriter** 1.0 or later, for the Excel export (F-022, D-025)
- CMake 3.21 or later

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Packages, verified versions and troubleshooting are in
[`BUILD.md`](../BUILD.md). Tests run headless and never touch the network:
providers are answered from recorded responses in `tests/fixtures/`.

---

## Project structure

```
pinax/
├── db/
│   ├── schema.sql        schema version 6, always the latest in full
│   └── migrations/       NNN_*.sql, one step per version, for older files
├── CMakeLists.txt
├── src/                  one directory per module, per ARCHITECTURE.md §2
│   ├── main.cpp
│   ├── app/              composition root, settings, main window shell
│   ├── domain/           plain value types, no Qt or SQL
│   ├── db/               connection, migrations, repositories
│   ├── metadata/         Open Library, Google Books, British Library, Wikidata, cover cache
│   ├── io/               CSV import, export, backup
│   └── ui/               rail, list, detail panel
├── tests/                Qt Test, run by ctest
│   └── fixtures/         recorded provider responses; frozen schema_v1.sql
├── docs/
│   ├── USER_GUIDE.md     using Pinax
│   ├── DEVELOPER.md      this page
│   └── screenshots/      the README's and the guide's, from a demo catalogue
├── design/
│   ├── Pinax UI.html     interactive mock-up, four screens
│   └── screens/          the same four screens as PNG
├── seed/                 the real catalogue; git-ignored, never committed
├── README.md
├── BUILD.md              requirements, build, test, troubleshooting
├── FEATURES.md           F-001 … F-030, MoSCoW priorities, acceptance criteria
├── ROADMAP.md            Phases 0–4 complete; Phase 5 planned
├── ARCHITECTURE.md       modules, data flow, invariants
├── DECISIONS.md          D-001 … D-031, append-only
├── SPEC.md               CSV format, ISBN validation, provider contracts, exports
├── ATTACK_VECTORS.md     AV-001 … AV-014, failure modes with detection
├── BUGS.md               BUG-001 … BUG-006, all fixed
├── IMPROVEMENTS.md       IMP-001 … IMP-011
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

## The database on its own

A catalogue is an ordinary SQLite file, usable without Pinax:

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

Any client connecting to the database must issue `PRAGMA foreign_keys = ON;`
on each connection. SQLite does not persist that setting, and without it the
foreign keys in the schema are advisory only (AV-004).

---

## Documentation map

| Document | Contents |
|---|---|
| [`FEATURES.md`](../FEATURES.md) | What it does, what it will not do, and what is merely a candidate |
| [`ROADMAP.md`](../ROADMAP.md) | Phases, deliverables and acceptance criteria |
| [`ARCHITECTURE.md`](../ARCHITECTURE.md) | Modules, data flow, invariants — descriptive only |
| [`DECISIONS.md`](../DECISIONS.md) | Why it ended up this way, with reversal conditions |
| [`SPEC.md`](../SPEC.md) | Formats and protocols: CSV, ISBN, providers, exports |
| [`ATTACK_VECTORS.md`](../ATTACK_VECTORS.md) | How it can go wrong, and how that is detected |
| [`BUGS.md`](../BUGS.md) · [`IMPROVEMENTS.md`](../IMPROVEMENTS.md) | Realised defects, and candidate refactors |
| [`CLAUDE.md`](../CLAUDE.md) | Handoff for AI development sessions |
| [`CHANGELOG.md`](../CHANGELOG.md) | Version history with ID traceability |
| [`BUILD.md`](../BUILD.md) | Requirements, build and test commands, troubleshooting |
