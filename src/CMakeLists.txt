# Composition root and window shell. Further modules (domain, db, metadata,
# io, ui) are added here as static libraries as each one gains code.
add_library(pinax_app STATIC
    app/main_window.cpp
    app/main_window.h)

target_include_directories(pinax_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pinax_app PUBLIC Qt6::Widgets)

qt_add_executable(pinax main.cpp)
target_link_libraries(pinax PRIVATE pinax_app)
target_compile_definitions(pinax PRIVATE PINAX_VERSION="${PROJECT_VERSION}")
