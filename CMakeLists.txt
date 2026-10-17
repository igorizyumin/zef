cmake_minimum_required(VERSION 3.16)

project(zef VERSION 0.1.0 LANGUAGES C)

include(CTest)

option(ZEF_ENABLE_COVERAGE "Build LLVM source-based coverage instrumentation" OFF)
option(ZEF_ENABLE_ASSERTIONS "Enable ZEF_REQUIRE precondition checks" ON)
option(ZEF_BUILD_ARM_CM0P_PORT "Build the Cortex-M0+ shared-stack Port" OFF)
option(ZEF_BUILD_NUCLEO_L073RZ_EXAMPLE
       "Build the bare-metal Nucleo-L073RZ framework example" OFF)

if (ZEF_ENABLE_COVERAGE AND NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "ZEF_ENABLE_COVERAGE requires a Clang compiler")
endif()

add_library(zef
    src/zef_actor.c
    src/zef_kernel.c
    src/zef_machine.c
    src/zef_mailbox.c
    src/zef_message.c
    src/zef_port.c
    src/zef_timer.c
)

target_compile_features(zef PUBLIC c_std_11)
target_include_directories(zef PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(zef PUBLIC
    ZEF_ENABLE_ASSERTIONS=$<BOOL:${ZEF_ENABLE_ASSERTIONS}>
)

if (ZEF_BUILD_ARM_CM0P_PORT)
    add_library(zef_arm_cm0p_port STATIC
        ports/arm-cm0p/zef_arm_cm0p_port.c
    )
    target_include_directories(zef_arm_cm0p_port
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(zef_arm_cm0p_port PUBLIC zef)
    target_compile_features(zef_arm_cm0p_port PUBLIC c_std_11)
endif()

if (ZEF_BUILD_NUCLEO_L073RZ_EXAMPLE)
    if (NOT ZEF_BUILD_ARM_CM0P_PORT)
        message(FATAL_ERROR
            "ZEF_BUILD_NUCLEO_L073RZ_EXAMPLE requires ZEF_BUILD_ARM_CM0P_PORT=ON")
    endif()
    add_subdirectory(examples/nucleo-l073rz)
endif()

if (MSVC)
    target_compile_options(zef PRIVATE /W4)
else()
    target_compile_options(zef PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (ZEF_ENABLE_COVERAGE)
    target_compile_options(zef PRIVATE -fprofile-instr-generate -fcoverage-mapping)
endif()

if (BUILD_TESTING)
    add_executable(zef_smoke_test tests/zef_smoke_test.c)
    target_link_libraries(zef_smoke_test PRIVATE zef)
    target_compile_features(zef_smoke_test PRIVATE c_std_11)
    if (NOT MSVC)
        target_compile_options(zef_smoke_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if (ZEF_ENABLE_COVERAGE)
        target_compile_options(zef_smoke_test PRIVATE -fprofile-instr-generate -fcoverage-mapping)
        target_link_options(zef_smoke_test PRIVATE -fprofile-instr-generate)
    endif()
    add_test(NAME zef_smoke_test COMMAND zef_smoke_test)

    add_executable(zef_test_work tests/zef_test_work.c)
    target_link_libraries(zef_test_work PRIVATE zef)
    target_compile_features(zef_test_work PRIVATE c_std_11)
    if (NOT MSVC)
        target_compile_options(zef_test_work PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if (ZEF_ENABLE_COVERAGE)
        target_compile_options(zef_test_work PRIVATE -fprofile-instr-generate -fcoverage-mapping)
        target_link_options(zef_test_work PRIVATE -fprofile-instr-generate)
    endif()
    add_test(NAME zef_test_work COMMAND zef_test_work)

    if (ZEF_ENABLE_COVERAGE)
        find_program(ZEF_LLVM_PROFDATA llvm-profdata REQUIRED)
        find_program(ZEF_LLVM_COV llvm-cov REQUIRED)
        add_custom_target(coverage
            COMMAND ${CMAKE_COMMAND}
                -DZEF_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DZEF_CONFIGURATION=$<CONFIG>
                -DZEF_COVERAGE_DIR=${CMAKE_CURRENT_BINARY_DIR}/coverage
                -DZEF_LLVM_PROFDATA=${ZEF_LLVM_PROFDATA}
                -DZEF_LLVM_COV=${ZEF_LLVM_COV}
                -DZEF_WORK_TEST=$<TARGET_FILE:zef_test_work>
                -DZEF_SMOKE_TEST=$<TARGET_FILE:zef_smoke_test>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_coverage.cmake
            DEPENDS zef_smoke_test zef_test_work
            USES_TERMINAL
        )
    endif()
endif()
