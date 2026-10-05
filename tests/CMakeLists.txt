qt_add_executable(test_main_window test_main_window.cpp)
target_link_libraries(test_main_window PRIVATE pinax_app Qt6::Test)

add_test(NAME main_window COMMAND test_main_window)
# Tests run without a display.
set_tests_properties(main_window PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
