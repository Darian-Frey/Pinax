# Each test is a Qt Test executable registered with ctest. Tests run without
# a display.
function(pinax_add_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARGN})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

pinax_add_test(test_main_window pinax_app)
pinax_add_test(test_domain pinax_domain)
pinax_add_test(test_db pinax_db)
target_compile_definitions(test_db PRIVATE
    PINAX_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
pinax_add_test(test_book_list pinax_ui)
pinax_add_test(test_rail pinax_ui)
pinax_add_test(test_series_page pinax_ui)
pinax_add_test(test_entry_editor pinax_ui)
pinax_add_test(test_import pinax_io)
pinax_add_test(test_series_import pinax_io)
target_compile_definitions(test_series_import PRIVATE PINAX_SEED_DIR="${PROJECT_SOURCE_DIR}/seed")
pinax_add_test(test_detail_panel pinax_ui)
pinax_add_test(test_catalogue pinax_app pinax_io)
# The owner's real catalogue, if present; the test skips without it.
target_compile_definitions(test_import PRIVATE
    PINAX_SEED_CSV="${PROJECT_SOURCE_DIR}/seed/library.csv")
