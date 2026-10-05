# One static library per module (ARCHITECTURE.md §2), linked by the
# executable and by whichever tests need them.

# domain: plain value types, no Qt or SQL.
add_library(pinax_domain STATIC
    domain/book.h
    domain/enums.cpp
    domain/enums.h
    domain/sort_title.cpp
    domain/sort_title.h)
target_include_directories(pinax_domain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(pinax_domain PROPERTIES AUTOMOC OFF)

# db: the only module that issues SQL (D-015). No Qt dependency.
# The schema is compiled in, so the application needs nothing from db/ at run
# time; editing db/schema.sql re-runs the configure step.
file(READ ${PROJECT_SOURCE_DIR}/db/schema.sql PINAX_SCHEMA_SQL)
configure_file(db/schema_sql.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/db/schema_sql.h @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/db/schema.sql)

add_library(pinax_db STATIC
    db/book_repository.cpp
    db/book_repository.h
    db/connection.cpp
    db/connection.h
    db/db_error.h
    db/migrations.cpp
    db/migrations.h
    db/statement.cpp
    db/statement.h
    db/transaction.cpp
    db/transaction.h)
target_include_directories(pinax_db
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(pinax_db PUBLIC pinax_domain SQLite::SQLite3)
set_target_properties(pinax_db PROPERTIES AUTOMOC OFF)

# app: composition root and window shell.
add_library(pinax_app STATIC
    app/main_window.cpp
    app/main_window.h)
target_include_directories(pinax_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pinax_app PUBLIC Qt6::Widgets)

qt_add_executable(pinax main.cpp)
target_link_libraries(pinax PRIVATE pinax_app pinax_db)
target_compile_definitions(pinax PRIVATE PINAX_VERSION="${PROJECT_VERSION}")
