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

Ninja is optional; without `-G Ninja` CMake falls back to Make.

```sh
sudo apt install g++ cmake ninja-build qt6-base-dev
```

Later phases add dependencies, each recorded here when it is first needed:
SQLite development headers and Qt SQL in Phase 1, libxlsxwriter in Phase 4
(F-022), and optionally Qt6 Multimedia and ZXing-C++ in Phase 5 (D-014).

---

## Build

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

The application is `build/src/pinax`.

## Test

```sh
ctest --test-dir build --output-on-failure
```

Tests set `QT_QPA_PLATFORM=offscreen`, so they run without a display.

---

## Layout

| Target | Kind | Contents |
|---|---|---|
| `pinax_app` | static library | `src/app/` — composition root and window shell |
| `pinax` | executable | `src/main.cpp` |
| `test_main_window` | test | `tests/test_main_window.cpp` |

Each module in ARCHITECTURE.md §2 becomes its own static library under `src/`
as it gains code, so tests link only what they exercise.

---

## Troubleshooting

- **`Could not find a package configuration file provided by "Qt6"`** —
  `qt6-base-dev` is not installed, or a second Qt is shadowing it. Pass
  `-DCMAKE_PREFIX_PATH=/usr/lib/x86_64-linux-gnu/cmake/Qt6` to point at the
  system Qt.
- **`qt.qpa.xcb: could not connect to display`** when running a test binary
  directly — set `QT_QPA_PLATFORM=offscreen`, which `ctest` does for you.
