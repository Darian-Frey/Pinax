# Building Pinax

Verified on Linux Mint 22.3 (Ubuntu 24.04 base), 2026-10-05.

---

## Requirements

| Tool | Minimum | Verified with | Package (Ubuntu / Mint) |
|---|---|---|---|
| C++ compiler with C++20 | GCC 11 | GCC 13.3 | `g++` |
| CMake | 3.21 | 3.28.3 | `cmake` |
| Ninja | — | 1.11.1 | `ninja-build` |
| Qt6 Widgets, Test | 6.4 | 6.4.2 | `qt6-base-dev` |
| SQLite headers | 3.31 | 3.45.1 | `libsqlite3-dev` |

Ninja is optional; without `-G Ninja` CMake falls back to Make.

```sh
sudo apt install g++ cmake ninja-build qt6-base-dev libsqlite3-dev
```

Later phases add dependencies, each recorded here when it is first needed:
Qt Network in Phase 3, libxlsxwriter in Phase 4 (F-022), and optionally Qt6
Multimedia and ZXing-C++ in Phase 5 (D-014). Qt SQL is not used (D-015).

---

## Build

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

The application is `build/src/pinax`. It opens `~/.local/share/pinax/pinax.db`,
creating and migrating it on first run, or the file given as its argument:

```sh
build/src/pinax /path/to/other.db
```

To import a CSV first (SPEC.md §1, safe to repeat):

```sh
build/src/pinax --import seed/library.csv
```

`db/schema.sql` is compiled in; editing it re-runs the CMake configure step
automatically.

## Test

```sh
ctest --test-dir build --output-on-failure
```

Tests set `QT_QPA_PLATFORM=offscreen`, so they run without a display.

---

## Layout

| Target | Kind | Contents |
|---|---|---|
| `pinax_domain` | static library | `src/domain/` — value types, no Qt or SQL |
| `pinax_db` | static library | `src/db/` — SQLite wrapper, migrations, repositories; no Qt |
| `pinax_io` | static library | `src/io/` — CSV reader, importer, sort-position derivation; no Qt |
| `pinax_ui` | static library | `src/ui/` — list model, sort proxy, list view; no `db` |
| `pinax_app` | static library | `src/app/` — `Catalogue` and the window shell; links `ui` and `db` |
| `pinax` | executable | `src/main.cpp` |
| `test_main_window`, `test_domain`, `test_db`, `test_book_list`, `test_rail`, `test_series_page`, `test_entry_editor`, `test_import`, `test_series_import`, `test_detail_panel`, `test_catalogue` | tests | `tests/<name>.cpp`, added with `pinax_add_test` |

`db/migrations/NNN_description.sql` files are picked up by a configure-time
glob; adding one re-runs configure on the next build. `NNN` is the schema
version the step produces. `tests/fixtures/schema_v1.sql` is a frozen version
1 schema that `test_db` migrates forward; never edit it.

`test_import`'s `seedCatalogueImportsInOnePass` reads the owner's real
catalogue from `seed/library.csv` when it exists and skips otherwise, so a
clone without `seed/` still passes. `test_series_import`'s
`seedSeriesMatchTheSpreadsheet` does the same with `seed/series.csv`.

Each module in ARCHITECTURE.md §2 becomes its own static library under `src/`
as it gains code, so tests link only what they exercise.

---

## Troubleshooting

- **`Could not find a package configuration file provided by "Qt6"`** —
  `qt6-base-dev` is not installed, or a second Qt is shadowing it. Pass
  `-DCMAKE_PREFIX_PATH=/usr/lib/x86_64-linux-gnu/cmake/Qt6` to point at the
  system Qt.
- **`Could NOT find SQLite3`** — install `libsqlite3-dev`.
- **`qt.qpa.xcb: could not connect to display`** when running a test binary
  directly — set `QT_QPA_PLATFORM=offscreen`, which `ctest` does for you.
