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
