set(ZEF_NUCLEO_TARGET nucleo_l073rz_zef_demo)
set(ZEF_NUCLEO_CPU_FLAGS -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft)
set(ZEF_NUCLEO_COMMON_FLAGS
    ${ZEF_NUCLEO_CPU_FLAGS}
    -ffunction-sections
    -fdata-sections
    -fno-common
    -Os
    -Wall
    -Wextra
    -Wpedantic
)
set(ZEF_NUCLEO_EXECUTION_CONFIG
    "${CMAKE_CURRENT_SOURCE_DIR}/zef_nucleo_execution_config.h")
list(APPEND ZEF_NUCLEO_COMMON_FLAGS
    -include ${ZEF_NUCLEO_EXECUTION_CONFIG})

# The framework and its port are ordinary CMake targets, so apply the target
# CPU flags to them as well as to the application.
target_compile_options(zef PRIVATE ${ZEF_NUCLEO_COMMON_FLAGS})
target_compile_options(zef_arm_cm0p_port PRIVATE ${ZEF_NUCLEO_COMMON_FLAGS})

add_executable(${ZEF_NUCLEO_TARGET}
    main.c
    demo_app.c
    demo_serial_actor.c
    demo_timer_source.c
    demo_worker_actor.c
    nucleo_l073rz_hw.c
    startup_stm32l073xx.c
)

target_compile_options(${ZEF_NUCLEO_TARGET} PRIVATE ${ZEF_NUCLEO_COMMON_FLAGS})
target_include_directories(${ZEF_NUCLEO_TARGET} PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/ports/arm-cm0p
)
target_link_libraries(${ZEF_NUCLEO_TARGET} PRIVATE
    zef_arm_cm0p_port
    zef
    c
    gcc
)
target_link_options(${ZEF_NUCLEO_TARGET} PRIVATE
    ${ZEF_NUCLEO_CPU_FLAGS}
    -T${CMAKE_CURRENT_SOURCE_DIR}/STM32L073RZTx_FLASH.ld
    -Wl,--gc-sections
    -Wl,--Map=${CMAKE_CURRENT_BINARY_DIR}/${ZEF_NUCLEO_TARGET}.map
    -nostartfiles
    -nodefaultlibs
    -nostdlib
)

get_filename_component(ZEF_NUCLEO_TOOLCHAIN_BIN
    "${CMAKE_C_COMPILER}" DIRECTORY)
find_program(ZEF_NUCLEO_OBJCOPY arm-none-eabi-objcopy
    HINTS "${ZEF_NUCLEO_TOOLCHAIN_BIN}" REQUIRED)
find_program(ZEF_NUCLEO_SIZE arm-none-eabi-size
    HINTS "${ZEF_NUCLEO_TOOLCHAIN_BIN}" REQUIRED)

add_custom_command(TARGET ${ZEF_NUCLEO_TARGET} POST_BUILD
    COMMAND ${ZEF_NUCLEO_SIZE} $<TARGET_FILE:${ZEF_NUCLEO_TARGET}>
    COMMAND ${ZEF_NUCLEO_OBJCOPY} -O ihex
            $<TARGET_FILE:${ZEF_NUCLEO_TARGET}>
            ${CMAKE_CURRENT_BINARY_DIR}/${ZEF_NUCLEO_TARGET}.hex
    COMMAND ${ZEF_NUCLEO_OBJCOPY} -O binary
            $<TARGET_FILE:${ZEF_NUCLEO_TARGET}>
            ${CMAKE_CURRENT_BINARY_DIR}/${ZEF_NUCLEO_TARGET}.bin
    VERBATIM
)
