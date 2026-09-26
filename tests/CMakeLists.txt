qt_add_executable(tst_core tst_core.cpp)
target_link_libraries(tst_core PRIVATE chcore Qt6::Test)
add_test(NAME tst_core COMMAND tst_core)
set_tests_properties(tst_core PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
