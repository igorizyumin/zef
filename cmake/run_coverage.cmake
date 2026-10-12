if (NOT DEFINED ZEF_BUILD_DIR OR NOT DEFINED ZEF_CONFIGURATION OR
    NOT DEFINED ZEF_COVERAGE_DIR OR NOT DEFINED ZEF_LLVM_PROFDATA OR
    NOT DEFINED ZEF_LLVM_COV OR NOT DEFINED ZEF_WORK_TEST OR
    NOT DEFINED ZEF_SMOKE_TEST)
    message(FATAL_ERROR "Coverage runner received incomplete configuration")
endif()

file(REMOVE_RECURSE "${ZEF_COVERAGE_DIR}")
file(MAKE_DIRECTORY "${ZEF_COVERAGE_DIR}")

set(ENV{LLVM_PROFILE_FILE} "${ZEF_COVERAGE_DIR}/%p-%m.profraw")
execute_process(
    COMMAND "${CMAKE_CTEST_COMMAND}"
            --test-dir "${ZEF_BUILD_DIR}"
            -C "${ZEF_CONFIGURATION}"
            --output-on-failure
    RESULT_VARIABLE test_result
)
if (NOT test_result EQUAL 0)
    message(FATAL_ERROR "Instrumented tests failed with exit code ${test_result}")
endif()

file(GLOB profile_files "${ZEF_COVERAGE_DIR}/*.profraw")
list(LENGTH profile_files profile_count)
if (profile_count EQUAL 0)
    message(FATAL_ERROR "No LLVM profile files were produced")
endif()

set(profile_data "${ZEF_COVERAGE_DIR}/zef.profdata")
execute_process(
    COMMAND "${ZEF_LLVM_PROFDATA}" merge -sparse ${profile_files}
            -o "${profile_data}"
    RESULT_VARIABLE profdata_result
)
if (NOT profdata_result EQUAL 0)
    message(FATAL_ERROR "llvm-profdata failed with exit code ${profdata_result}")
endif()

message(STATUS "LLVM coverage summary")
execute_process(
    COMMAND "${ZEF_LLVM_COV}" report "${ZEF_WORK_TEST}"
            -object "${ZEF_SMOKE_TEST}"
            -instr-profile "${profile_data}"
            -show-branch-summary
    RESULT_VARIABLE report_result
)
if (NOT report_result EQUAL 0)
    message(FATAL_ERROR "llvm-cov report failed with exit code ${report_result}")
endif()

set(html_dir "${ZEF_COVERAGE_DIR}/html")
file(MAKE_DIRECTORY "${html_dir}")
execute_process(
    COMMAND "${ZEF_LLVM_COV}" show "${ZEF_WORK_TEST}"
            -object "${ZEF_SMOKE_TEST}"
            -instr-profile "${profile_data}"
            -format=html
            -output-dir "${html_dir}"
            -show-branch-summary
    RESULT_VARIABLE html_result
)
if (NOT html_result EQUAL 0)
    message(FATAL_ERROR "llvm-cov HTML export failed with exit code ${html_result}")
endif()

message(STATUS "HTML coverage report: ${html_dir}")
