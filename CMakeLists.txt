cmake_minimum_required(VERSION 3.21)

project(pinax
    VERSION 0.0.1
    DESCRIPTION "Catalogue for a personal physical library"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network Test)
# 3.31 for VACUUM INTO and partial indexes (README, Build requirements).
find_package(SQLite3 3.31 REQUIRED)
# libxlsxwriter for the Excel export (F-022, D-025): libxlsxwriter-dev.
find_package(PkgConfig REQUIRED)
pkg_check_modules(XLSXWRITER REQUIRED IMPORTED_TARGET xlsxwriter>=1.0)
qt_standard_project_setup()

add_compile_options(-Wall -Wextra -Wpedantic)

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)
