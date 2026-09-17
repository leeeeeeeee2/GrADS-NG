# GrADS-NG tests (M1): unit tests + CLI integration tests via CTest.
set(NG_INCLUDE ${CMAKE_SOURCE_DIR}/include)

# --- Unit: expression lexer/parser/evaluator ---
add_executable(test_expr test_expr.c)
target_include_directories(test_expr PRIVATE ${NG_INCLUDE})
target_link_libraries(test_expr PRIVATE ngparser m)
add_test(NAME expr_unit COMMAND test_expr)

# --- Unit: descriptor (.ctl) parser ---
add_executable(test_ctl test_ctl.c)
target_link_libraries(test_ctl PRIVATE ngio)
add_test(NAME ctl_unit COMMAND test_ctl ${CMAKE_CURRENT_SOURCE_DIR}/data)

# --- Unit: binary grid reader ---
add_executable(test_grid test_grid.c)
target_link_libraries(test_grid PRIVATE ngio)
add_test(NAME grid_unit COMMAND test_grid ${CMAKE_CURRENT_SOURCE_DIR}/data)

# --- Unit: array expression evaluator ---
add_executable(test_eval test_eval.c)
target_include_directories(test_eval PRIVATE
    ${NG_INCLUDE} ${CMAKE_SOURCE_DIR}/src/eval)
target_link_libraries(test_eval PRIVATE ngcore m)
add_test(NAME eval_unit COMMAND test_eval ${CMAKE_CURRENT_SOURCE_DIR}/data)

# --- Unit: session lifecycle + file accessors ---
add_executable(test_core test_core.c)
target_include_directories(test_core PRIVATE ${NG_INCLUDE})
target_link_libraries(test_core PRIVATE ngcore)
add_test(NAME core_unit COMMAND test_core ${CMAKE_CURRENT_SOURCE_DIR}/data/sample.ctl)

# --- CLI integration ---
set(GRADS_NG_BIN $<TARGET_FILE:grads-ng>)
set(SAMPLE_CTL ${CMAKE_CURRENT_SOURCE_DIR}/data/sample.ctl)

add_test(NAME cli_version COMMAND grads-ng -v)
set_tests_properties(cli_version PROPERTIES PASS_REGULAR_EXPRESSION "GrADS-NG 0\\.1\\.0")

add_test(NAME cli_help COMMAND grads-ng -h)
set_tests_properties(cli_help PROPERTIES PASS_REGULAR_EXPRESSION "Usage:")

add_test(NAME cli_expr COMMAND grads-ng -e "2+3*4")
set_tests_properties(cli_expr PROPERTIES PASS_REGULAR_EXPRESSION "^14")

add_test(NAME cli_expr_power COMMAND grads-ng -e "-2^2")
set_tests_properties(cli_expr_power PROPERTIES PASS_REGULAR_EXPRESSION "^-4")

add_test(NAME cli_expr_error COMMAND grads-ng -e "1/0")
set_tests_properties(cli_expr_error PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli_expr_undef COMMAND grads-ng -e "tsfc+1")
set_tests_properties(cli_expr_undef PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli_inspect COMMAND grads-ng -f ${SAMPLE_CTL})
set_tests_properties(cli_inspect PROPERTIES
    PASS_REGULAR_EXPRESSION "Dimensions: 4 x 3 x 2 x 2")

add_test(NAME cli_open_missing COMMAND grads-ng -c "open /nonexistent/x.ctl")
set_tests_properties(cli_open_missing PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli_q_no_files COMMAND grads-ng -c "q file")
set_tests_properties(cli_q_no_files PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli_unknown COMMAND grads-ng -c "frobnicate")
set_tests_properties(cli_unknown PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli_future_cmd COMMAND grads-ng -c "set lon 1")
set_tests_properties(cli_future_cmd PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli_d_no_file COMMAND grads-ng -c "d tsfc")
set_tests_properties(cli_d_no_file PROPERTIES WILL_FAIL TRUE)

# End-to-end `d` through a piped REPL session (POSIX sh; CI is Linux).
add_test(NAME cli_d_values
    COMMAND sh -c "printf 'open ${SAMPLE_CTL}\nd tsfc\nquit\n' | ${GRADS_NG_BIN}")
set_tests_properties(cli_d_values PROPERTIES
    PASS_REGULAR_EXPRESSION "Displaying tsfc: 4 x 3")

add_test(NAME cli_d_unknown_var
    COMMAND sh -c "printf 'open ${SAMPLE_CTL}\nd nope\nquit\n' | ${GRADS_NG_BIN}")
set_tests_properties(cli_d_unknown_var PROPERTIES
    PASS_REGULAR_EXPRESSION "not defined;Available variables: tsfc hgt")

add_test(NAME cli_d_expression
    COMMAND sh -c "printf 'open ${SAMPLE_CTL}\nd tsfc+1\nquit\n' | ${GRADS_NG_BIN}")
set_tests_properties(cli_d_expression PROPERTIES
    PASS_REGULAR_EXPRESSION "Displaying tsfc\\+1: 4 x 3")

add_test(NAME cli_d_function
    COMMAND sh -c "printf 'open ${SAMPLE_CTL}\nd sqrt(tsfc)\nquit\n' | ${GRADS_NG_BIN}")
set_tests_properties(cli_d_function PROPERTIES
    PASS_REGULAR_EXPRESSION "Displaying sqrt\\(tsfc\\): 4 x 3")

add_test(NAME cli_d_maxmin
    COMMAND sh -c "printf 'open ${SAMPLE_CTL}\nd max(tsfc,t=1,t=2)\nquit\n' | ${GRADS_NG_BIN}")
set_tests_properties(cli_d_maxmin PROPERTIES
    PASS_REGULAR_EXPRESSION "Displaying max\\(tsfc,t=1,t=2\\): 4 x 3")

add_test(NAME cli_d_ave
    COMMAND sh -c "printf 'open ${SAMPLE_CTL}\nd ave(tsfc,t=1,t=2)\nquit\n' | ${GRADS_NG_BIN}")
set_tests_properties(cli_d_ave PROPERTIES
    PASS_REGULAR_EXPRESSION "Displaying ave\\(tsfc,t=1,t=2\\): 4 x 3")

add_test(NAME cli_d_badrange
    COMMAND sh -c "printf 'open ${SAMPLE_CTL}\nd max(tsfc,t=2,t=1)\nquit\n' | ${GRADS_NG_BIN}")
set_tests_properties(cli_d_badrange PROPERTIES
    PASS_REGULAR_EXPRESSION "past end")

add_test(NAME cli_set_dims
    COMMAND sh -c "printf 'open ${SAMPLE_CTL}\nset t 2\nset z 2\nq dims\nd hgt\nquit\n' | ${GRADS_NG_BIN}")
set_tests_properties(cli_set_dims PROPERTIES
    PASS_REGULAR_EXPRESSION "T: 2 of 2;Displaying hgt: 4 x 3 \\(t=2, z=2\\)")

add_test(NAME cli_set_bad
    COMMAND sh -c "printf 'open ${SAMPLE_CTL}\nset t 9\nquit\n' | ${GRADS_NG_BIN}")
set_tests_properties(cli_set_bad PROPERTIES
    PASS_REGULAR_EXPRESSION "out of range")

add_test(NAME cli_set_unknown
    COMMAND sh -c "printf 'open ${SAMPLE_CTL}\nset lon 1\nquit\n' | ${GRADS_NG_BIN}")
set_tests_properties(cli_set_unknown PROPERTIES
    PASS_REGULAR_EXPRESSION "only 'set t N' and 'set z N'")

add_test(NAME cli_close_ok
    COMMAND sh -c "printf 'open ${SAMPLE_CTL}\nclose 1\nquit\n' | ${GRADS_NG_BIN}")
set_tests_properties(cli_close_ok PROPERTIES
    PASS_REGULAR_EXPRESSION "File 1 has been closed")

add_test(NAME cli_close_reopen
    COMMAND sh -c "printf 'open ${SAMPLE_CTL}\nclose 1\nq file\nopen ${SAMPLE_CTL}\nq file\nquit\n' | ${GRADS_NG_BIN}")
set_tests_properties(cli_close_reopen PROPERTIES
    PASS_REGULAR_EXPRESSION "File 1 has been closed.*no files are open.*File 1: .*sample.ctl")

add_test(NAME cli_close_order
    COMMAND sh -c "printf 'open ${SAMPLE_CTL}\nopen ${SAMPLE_CTL}\nclose 1\nclose 2\nq file\nquit\n' | ${GRADS_NG_BIN}")
set_tests_properties(cli_close_order PROPERTIES
    PASS_REGULAR_EXPRESSION "Only last file may be closed.*File 2 has been closed")

add_test(NAME cli_close_missing
    COMMAND sh -c "printf 'close\nquit\n' | ${GRADS_NG_BIN}")
set_tests_properties(cli_close_missing PROPERTIES
    PASS_REGULAR_EXPRESSION "Missing file number")

add_test(NAME cli_close_invalid
    COMMAND sh -c "printf 'open ${SAMPLE_CTL}\nclose foo\nquit\n' | ${GRADS_NG_BIN}")
set_tests_properties(cli_close_invalid PROPERTIES
    PASS_REGULAR_EXPRESSION "Invalid file number")

add_test(NAME cli_close_no_files COMMAND grads-ng -c "close 1")
set_tests_properties(cli_close_no_files PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli_close_trailing
    COMMAND sh -c "printf 'open ${SAMPLE_CTL}\nclose 1 extra junk\nquit\n' | ${GRADS_NG_BIN}")
set_tests_properties(cli_close_trailing PROPERTIES
    PASS_REGULAR_EXPRESSION "File 1 has been closed")
