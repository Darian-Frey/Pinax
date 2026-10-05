# One static library per module (ARCHITECTURE.md §2), linked by the
# executable and by whichever tests need them.

# domain: plain value types, no Qt or SQL.
add_library(pinax_domain STATIC
    domain/book.h
    domain/book_summary.h
    domain/enums.cpp
    domain/enums.h
    domain/sort_title.cpp
    domain/sort_title.h)
target_include_directories(pinax_domain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(pinax_domain PROPERTIES AUTOMOC OFF)

# db: the only module that issues SQL (D-015). No Qt dependency.
# The schema and its migration steps are compiled in, so the application needs
# nothing from db/ at run time; editing or adding any of them re-runs the
# configure step. Migration files are named NNN_description.sql, where NNN is
# the version the step produces.
file(READ ${PROJECT_SOURCE_DIR}/db/schema.sql PINAX_SCHEMA_SQL)
file(GLOB PINAX_MIGRATION_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/db/migrations/*.sql)
list(SORT PINAX_MIGRATION_FILES)
set(PINAX_MIGRATION_STEPS "")
foreach(migration IN LISTS PINAX_MIGRATION_FILES)
    get_filename_component(migration_name "${migration}" NAME)
    string(REGEX MATCH "^[0-9]+" migration_version "${migration_name}")
    if(NOT migration_version)
        message(FATAL_ERROR "Migration ${migration_name} does not start with its version number")
    endif()
    math(EXPR migration_version "${migration_version}")
    file(READ "${migration}" migration_sql)
    string(APPEND PINAX_MIGRATION_STEPS
        "    MigrationStep{${migration_version}, R\"pinax_schema(${migration_sql})pinax_schema\"},\n")
endforeach()
configure_file(db/schema_sql.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/db/schema_sql.h @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/db/schema.sql ${PINAX_MIGRATION_FILES})

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

# ui: Qt Widgets panels and item models. Fed domain types; issues no SQL
# (invariant 8), so it does not link pinax_db.
add_library(pinax_ui STATIC
    ui/book_list_model.cpp
    ui/book_list_model.h
    ui/book_list_view.cpp
    ui/book_list_view.h
    ui/book_sort_proxy.cpp
    ui/book_sort_proxy.h)
target_include_directories(pinax_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pinax_ui PUBLIC pinax_domain Qt6::Widgets)

# app: composition root and window shell.
add_library(pinax_app STATIC
    app/main_window.cpp
    app/main_window.h)
target_include_directories(pinax_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pinax_app PUBLIC pinax_ui Qt6::Widgets)

qt_add_executable(pinax main.cpp)
target_link_libraries(pinax PRIVATE pinax_app pinax_db)
target_compile_definitions(pinax PRIVATE PINAX_VERSION="${PROJECT_VERSION}")
