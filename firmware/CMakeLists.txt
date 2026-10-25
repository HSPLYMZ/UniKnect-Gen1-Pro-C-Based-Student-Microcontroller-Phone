cmake_minimum_required(VERSION 3.20)
project(uniknect_c C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(TARGET uniknect_c)
set(TP ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
option(APP_MODEM_ENABLE "Enable the EC200U AT and phone services" ON)
option(APP_PHONE_ONLY "Run only clock, LTE, call, SMS and UI services" ON)
option(APP_LCD_DIAGNOSTIC "Use verified color bars instead of application pages" OFF)
option(PHONE_PROBE_ENABLE "Enable legacy automatic one-number voice probe" OFF)

# ---------------------------------------------------------------- sources ---
set(APP_SOURCES
    app/main.c
    app/app_logic.c
    app/app_runtime.c
    app/app_services.c
    app/joy_input.c
    app/storage_logic.c
    app/voice_logic.c
    app/voice_service.c
    app/settings_data.c
    app/settings_store.c
    app/settings_service.c
    drivers/settings_flash.c
    app/signal_data.c
    app/signal_service.c
    app/app_storage.c
    app/app_console.c
    app/app_motion.c
    app/phone_logic.c
    app/phone_time.c
    app/phone_modem.c
    app/orientation.c
    app/ui_display.c
    app/ui_phone.c
    third_party/wououi/WouoUI_anim.c
    third_party/wououi/WouoUI_graph.c
    third_party/wououi/WouoUI_font.c
    fonts/phone_font.c
    drivers/adc_bus.c
    drivers/adc_ldr.c
    drivers/joystick.c
    drivers/sensor_i2c.c
    drivers/lis2dh12.c
    drivers/lcd_st7735.c
    osal/newlib_locks.c
    bsp/board.c
    bsp/system_stm32f4xx.c
    bsp/vector_table.c
    bsp/startup_stm32f413xx.s
)

# Quectel's RT-Thread-derived at_client with local safety patches (Apache-2.0).
# See third_party/at_client/LOCAL_CHANGES.md before updating this dependency.
set(AT_SOURCES)
if(APP_MODEM_ENABLE)
    list(APPEND APP_SOURCES app/at_probe.c
         drivers/ringbuffer.c drivers/uart_at.c osal/osal_freertos.c)
    list(APPEND AT_SOURCES third_party/at_client/src/at_client.c
         third_party/at_client/src/at_utils.c)
endif()
if(PHONE_PROBE_ENABLE)
    if(NOT APP_MODEM_ENABLE)
        message(FATAL_ERROR "PHONE_PROBE_ENABLE needs APP_MODEM_ENABLE")
    endif()
    list(APPEND APP_SOURCES app/phone_probe.c)
endif()
if(APP_LCD_DIAGNOSTIC)
    list(APPEND APP_SOURCES app/lcd_probe.c)
endif()

set(FREERTOS_SOURCES
    ${TP}/FreeRTOS-Kernel/tasks.c
    ${TP}/FreeRTOS-Kernel/queue.c
    ${TP}/FreeRTOS-Kernel/list.c
    ${TP}/FreeRTOS-Kernel/timers.c
    ${TP}/FreeRTOS-Kernel/event_groups.c
    ${TP}/FreeRTOS-Kernel/stream_buffer.c
    ${TP}/FreeRTOS-Kernel/portable/GCC/ARM_CM4F/port.c
    ${TP}/FreeRTOS-Kernel/portable/MemMang/heap_4.c
)

add_executable(${TARGET} ${APP_SOURCES} ${AT_SOURCES} ${FREERTOS_SOURCES})
target_compile_definitions(${TARGET} PRIVATE
    APP_MODEM_ENABLE=$<BOOL:${APP_MODEM_ENABLE}>
    APP_PHONE_ONLY=$<BOOL:${APP_PHONE_ONLY}>
    APP_LCD_DIAGNOSTIC=$<BOOL:${APP_LCD_DIAGNOSTIC}>
    PHONE_PROBE_ENABLE=$<BOOL:${PHONE_PROBE_ENABLE}>)

# Local AT safety patches are compiled with the same format warnings as BSP.

# Without CMAKE_SYSTEM_NAME the linker output has no extension; keep the usual
# .elf name so tools and debuggers find it.
set_target_properties(${TARGET} PROPERTIES SUFFIX ".elf")

# ------------------------------------------------------------- toolchain ----
get_filename_component(PUBLIC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
target_compile_options(${TARGET} PRIVATE "-ffile-prefix-map=${PUBLIC_ROOT}=.")
set(CPU_FLAGS -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)

target_compile_options(${TARGET} PRIVATE
    ${CPU_FLAGS}
    -O2 -g3
    -ffunction-sections -fdata-sections
    -fno-common
    -Wall -Wextra
    -Wno-unused-parameter
    -DSTM32F413xx
    -DHSE_VALUE=8000000
)

target_include_directories(${TARGET} PRIVATE
    config
    bsp
    app
    drivers
    fonts
    third_party/wououi
    osal
    third_party/at_client/include
    ${TP}/cmsis_core/Include
    ${TP}/cmsis_device_f4/Include
    ${TP}/FreeRTOS-Kernel/include
    ${TP}/FreeRTOS-Kernel/portable/GCC/ARM_CM4F
)

target_link_options(${TARGET} PRIVATE
    ${CPU_FLAGS}
    -T${CMAKE_CURRENT_SOURCE_DIR}/bsp/STM32F413ZHTx_FLASH.ld
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_BINARY_DIR}/${TARGET}.map,--cref
    -Wl,--print-memory-usage
    -Wl,--no-warn-rwx-segments
    -specs=nano.specs
    -u _printf_float
)

# ------------------------------------------------------------ post build ----
add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET}>
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${TARGET}> ${CMAKE_BINARY_DIR}/${TARGET}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET}> ${CMAKE_BINARY_DIR}/${TARGET}.bin
    COMMENT "size / hex / bin"
)

# Regenerate the vector table from the CMSIS device header when it changes.
# Generated into the source tree (not build/) so it is reviewable and can be
# committed; the rule only fires when the header or the generator changes.
if(NOT Python3_EXECUTABLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../.venv/Scripts/python.exe")
  set(Python3_EXECUTABLE "${CMAKE_CURRENT_SOURCE_DIR}/../.venv/Scripts/python.exe" CACHE FILEPATH "Python interpreter")
endif()
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(VECTOR_TABLE ${CMAKE_CURRENT_SOURCE_DIR}/bsp/vector_table.c)
add_custom_command(
    OUTPUT  ${VECTOR_TABLE}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/gen_vector_table.py
    DEPENDS ${TP}/cmsis_device_f4/Include/stm32f413xx.h
            ${CMAKE_CURRENT_SOURCE_DIR}/../tools/gen_vector_table.py
    COMMENT "regenerating bsp/vector_table.c"
    VERBATIM
)
add_custom_target(vectors DEPENDS ${VECTOR_TABLE})
