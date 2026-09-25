add_executable(spout_probe spout_probe.cpp)
target_link_libraries(spout_probe PRIVATE spoutdx)

set(APP $<TARGET_FILE:spout_stream>)

# --- Ligne de commande ---
add_test(NAME cli_help COMMAND ${APP} --help)
set_tests_properties(cli_help PROPERTIES PASS_REGULAR_EXPRESSION "Usage")

add_test(NAME cli_version COMMAND ${APP} --version)
set_tests_properties(cli_version PROPERTIES PASS_REGULAR_EXPRESSION "spout_stream ${APP_VERSION}")

add_test(NAME cli_list COMMAND ${APP} --list)
set_tests_properties(cli_list PROPERTIES PASS_REGULAR_EXPRESSION "Ecrans.*Fenetres")

add_test(NAME cli_no_source COMMAND ${APP} --name Test)
set_tests_properties(cli_no_source PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli_unknown_option COMMAND ${APP} --screen --foo)
set_tests_properties(cli_unknown_option PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli_missing_value COMMAND ${APP} --window)
set_tests_properties(cli_missing_value PROPERTIES WILL_FAIL TRUE)

# --- Bout en bout (capture reelle + recepteur Spout) ---
foreach(name IN ITEMS e2e_screen e2e_window)
    add_test(NAME ${name}
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/${name}.ps1
                -Exe ${APP} -Probe $<TARGET_FILE:spout_probe>)
    # Ces tests utilisent le bureau : jamais en parallele
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90 RESOURCE_LOCK desktop)
endforeach()
