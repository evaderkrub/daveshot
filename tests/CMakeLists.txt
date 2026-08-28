# Console test binaries. Both link daveshot_core, so they exercise the same
# code the application runs rather than a copy of it.

add_executable(daveshot_test_app test_app.cpp)
target_link_libraries(daveshot_test_app PRIVATE daveshot_core)
daveshot_set_warnings(daveshot_test_app)
add_test(NAME app COMMAND daveshot_test_app)

# End-to-end UI tests: a real ImGui context driven by the ImGui Test Engine,
# with no window and no GPU. The UI code under test is the same UiRoot the
# application draws.
add_executable(daveshot_test_ui test_ui.cpp)
target_link_libraries(daveshot_test_ui PRIVATE daveshot_core)
daveshot_set_warnings(daveshot_test_ui)
add_test(NAME ui COMMAND daveshot_test_ui)
