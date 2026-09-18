# Sequel Ace for Linux - tests
#
# tst_core exercises the pure logic (splitter, plist, favorites mapping, type
# mapping, DDL/DML builders). tst_integration needs a live server: set
# SA_TEST_MYSQL_HOST / SA_TEST_MYSQL_PORT / SA_TEST_MYSQL_USER /
# SA_TEST_MYSQL_PASSWORD (defaults 127.0.0.1 / 3306 / root / empty) and
# SA_TEST_MYSQL=1 to enable it; otherwise it skips.

qt_add_executable(tst_core tst_core.cpp)
target_link_libraries(tst_core PRIVATE sequelace_core Qt6::Test)
add_test(NAME tst_core COMMAND tst_core)

qt_add_executable(tst_integration tst_integration.cpp)
target_link_libraries(tst_integration PRIVATE sequelace_core Qt6::Test)
add_test(NAME tst_integration COMMAND tst_integration)
set_tests_properties(tst_core tst_integration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

if(SA_BUILD_APP)
    # Widget-level tests: drive the real views offscreen against a live server (SA_TEST_MYSQL=1).
    qt_add_executable(tst_ui tst_ui.cpp)
    target_link_libraries(tst_ui PRIVATE sequelace_ui Qt6::Test)
    add_test(NAME tst_ui COMMAND tst_ui)
    set_tests_properties(tst_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
