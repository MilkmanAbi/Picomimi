cmake_minimum_required(VERSION 3.13)

# Initialize Pico SDK
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(picomimi C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# ============================================================================
# Build Options
# ============================================================================

option(PICOMIMI_TARGET_RP2350 "Build for RP2350" OFF)
option(PICOMIMI_ENABLE_GOVERNOR "Enable CPU frequency governor" ON)
option(PICOMIMI_ENABLE_PMFS "Enable Picomimi filesystem" ON)
option(PICOMIMI_ENABLE_SHELL "Enable debug shell" ON)
option(PICOMIMI_ENABLE_RESOURCE_MANAGER "Enable resource manager" ON)
option(PICOMIMI_USB_STDIO "Use USB for stdio" ON)

# SD Card pins (configurable)
set(PICOMIMI_SD_CS_PIN "5" CACHE STRING "SD Card CS pin")
set(PICOMIMI_SD_SCK_PIN "18" CACHE STRING "SD Card SCK pin")
set(PICOMIMI_SD_MOSI_PIN "19" CACHE STRING "SD Card MOSI pin")
set(PICOMIMI_SD_MISO_PIN "16" CACHE STRING "SD Card MISO pin")

# ============================================================================
# Source Files
# ============================================================================

add_executable(picomimi
    src/main.c
    src/kernel/kernel.c
    src/kernel/scheduler.c
    src/power/governor.c
    src/ipc/ipc.c
    src/shell/shell.c
    src/memory/memory.c
    src/resource/resource.c
    src/oom/oom.c
    src/api/picomimi_api.c
    src/fs/pmfs.c
    src/fs/pmfs_ext.c
    src/drivers/drivers.c
    src/services/services.c
    src/utils/utils.c
    src/hal/gpio.c
    src/hal/spi.c
    src/hal/i2c.c
    src/hal/adc.c
    src/hal/pwm.c
    lib/sd/sd_card.c
    lib/fatfs/diskio.c
)

# ============================================================================
# Include Directories
# ============================================================================

target_include_directories(picomimi PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/oom
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/sd
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/fatfs
)

# ============================================================================
# Compile Definitions
# ============================================================================

target_compile_definitions(picomimi PRIVATE
    PICOMIMI_TARGET_RP2350=$<BOOL:${PICOMIMI_TARGET_RP2350}>
    PICOMIMI_ENABLE_GOVERNOR=$<BOOL:${PICOMIMI_ENABLE_GOVERNOR}>
    PICOMIMI_ENABLE_PMFS=$<BOOL:${PICOMIMI_ENABLE_PMFS}>
    PICOMIMI_ENABLE_SHELL=$<BOOL:${PICOMIMI_ENABLE_SHELL}>
    PICOMIMI_ENABLE_RESOURCE_MANAGER=$<BOOL:${PICOMIMI_ENABLE_RESOURCE_MANAGER}>
    PICOMIMI_SD_CS_PIN=${PICOMIMI_SD_CS_PIN}
    PICOMIMI_SD_SCK_PIN=${PICOMIMI_SD_SCK_PIN}
    PICOMIMI_SD_MOSI_PIN=${PICOMIMI_SD_MOSI_PIN}
    PICOMIMI_SD_MISO_PIN=${PICOMIMI_SD_MISO_PIN}
)

# ============================================================================
# Compiler Flags
# ============================================================================

target_compile_options(picomimi PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wno-unused-function
    -ffunction-sections
    -fdata-sections
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
)

# ============================================================================
# Linker Flags
# ============================================================================

target_link_options(picomimi PRIVATE
    -Wl,--gc-sections
)

# ============================================================================
# Pico SDK Libraries
# ============================================================================

target_link_libraries(picomimi
    pico_stdlib
    pico_multicore
    hardware_spi
    hardware_i2c
    hardware_adc
    hardware_pwm
    hardware_pio
    hardware_dma
    hardware_timer
    hardware_watchdog
    hardware_clocks
    hardware_vreg
    hardware_flash
)

# ============================================================================
# Stdio Configuration
# ============================================================================

if(PICOMIMI_USB_STDIO)
    pico_enable_stdio_usb(picomimi 1)
    pico_enable_stdio_uart(picomimi 0)
else()
    pico_enable_stdio_usb(picomimi 0)
    pico_enable_stdio_uart(picomimi 1)
endif()

# ============================================================================
# Output Files
# ============================================================================

pico_add_extra_outputs(picomimi)

# ============================================================================
# Build Info
# ============================================================================

if(PICOMIMI_TARGET_RP2350)
    set(TARGET_ARCH "cortex-m33")
else()
    set(TARGET_ARCH "cortex-m0plus")
endif()

message(STATUS "")
message(STATUS "═══════════════════════════════════════════════════════════")
message(STATUS "  PICOMIMI-AXISOS v15.0.0-Alpha Build Configuration")
message(STATUS "═══════════════════════════════════════════════════════════")
message(STATUS "  Target:       ${TARGET_ARCH}")
message(STATUS "  RP2350:       ${PICOMIMI_TARGET_RP2350}")
message(STATUS "  Governor:     ${PICOMIMI_ENABLE_GOVERNOR}")
message(STATUS "  PMFS:         ${PICOMIMI_ENABLE_PMFS}")
message(STATUS "  Shell:        ${PICOMIMI_ENABLE_SHELL}")
message(STATUS "  Resources:    ${PICOMIMI_ENABLE_RESOURCE_MANAGER}")
message(STATUS "  USB Stdio:    ${PICOMIMI_USB_STDIO}")
message(STATUS "  SD Pins:      CS=${PICOMIMI_SD_CS_PIN} SCK=${PICOMIMI_SD_SCK_PIN}")
message(STATUS "═══════════════════════════════════════════════════════════")
message(STATUS "")
