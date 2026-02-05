# ╔═══════════════════════════════════════════════════════════════════════════╗
# ║  MimiC - Self-Hosted C Compiler & Runtime for RP2040/RP2350               ║
# ╚═══════════════════════════════════════════════════════════════════════════╝

cmake_minimum_required(VERSION 3.13)

# Initialize pico-sdk from installed location
# (note: this can come from environment, CMake cache, etc.)
set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})

# Pull in Raspberry Pi Pico SDK
include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(mimic C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Raspberry Pi Pico SDK
pico_sdk_init()

# ============================================================================
# TARGET SELECTION
# ============================================================================

# Set target board (pico, pico2, etc.)
if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD "pico")
endif()

# Detect RP2350 vs RP2040
if(PICO_BOARD STREQUAL "pico2" OR PICO_BOARD STREQUAL "pico2_w")
    set(MIMIC_TARGET_RP2350 1)
    message(STATUS "Building for RP2350 (${PICO_BOARD})")
else()
    set(MIMIC_TARGET_RP2350 0)
    message(STATUS "Building for RP2040 (${PICO_BOARD})")
endif()

# ============================================================================
# MIMIC EXECUTABLE
# ============================================================================

add_executable(mimic
    # Main entry point
    src/main.c
    
    # Kernel
    src/kernel/mimic_kernel.c
    
    # Filesystem
    src/fs/mimic_fat32.c
    
    # Compiler
    src/compiler/mimic_lexer.c
    src/compiler/mimic_parser.c
    src/compiler/mimic_codegen.c
    src/compiler/mimic_linker.c
)

# Include directories
target_include_directories(mimic PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/include
)

# Target-specific defines
target_compile_definitions(mimic PRIVATE
    MIMIC_TARGET_RP2350=${MIMIC_TARGET_RP2350}
    MIMIC_VERSION_MAJOR=1
    MIMIC_VERSION_MINOR=0
    MIMIC_VERSION_PATCH=0
)

# Compiler flags
target_compile_options(mimic PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Os                     # Optimize for size
    -ffunction-sections     # Allow function-level garbage collection
    -fdata-sections
)

# Linker flags
target_link_options(mimic PRIVATE
    -Wl,--gc-sections       # Remove unused sections
)

# Link libraries
target_link_libraries(mimic
    pico_stdlib
    pico_multicore
    pico_sync
    hardware_spi
    hardware_gpio
    hardware_adc
    hardware_pwm
    hardware_timer
    hardware_watchdog
    hardware_flash
)

# Enable USB output, disable UART
pico_enable_stdio_usb(mimic 1)
pico_enable_stdio_uart(mimic 0)

# Create map/bin/hex/uf2 file
pico_add_extra_outputs(mimic)

# ============================================================================
# COMPILER COMPONENTS (individual targets for testing)
# ============================================================================

# Lexer test
add_executable(mimic_lex_test
    src/compiler/mimic_lexer.c
    src/fs/mimic_fat32.c
    test/test_lexer.c
)

target_include_directories(mimic_lex_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(mimic_lex_test PRIVATE
    MIMIC_TARGET_RP2350=${MIMIC_TARGET_RP2350}
)

target_link_libraries(mimic_lex_test
    pico_stdlib
    hardware_spi
    hardware_gpio
)

pico_enable_stdio_usb(mimic_lex_test 1)
pico_enable_stdio_uart(mimic_lex_test 0)
pico_add_extra_outputs(mimic_lex_test)

# ============================================================================
# CUSTOM TARGETS
# ============================================================================

# Generate SDK headers
add_custom_target(mimic_sdk
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/sdk/include
        ${CMAKE_BINARY_DIR}/sdk/include
    COMMENT "Copying SDK headers"
)

# Flash target
add_custom_target(flash
    COMMAND picotool load -f ${CMAKE_BINARY_DIR}/mimic.uf2
    DEPENDS mimic
    COMMENT "Flashing MimiC to device"
)

# ============================================================================
# BUILD INFO
# ============================================================================

message(STATUS "")
message(STATUS "╔═══════════════════════════════════════════════════════════════╗")
message(STATUS "║  MimiC Build Configuration                                    ║")
message(STATUS "╠═══════════════════════════════════════════════════════════════╣")
message(STATUS "║  Board:  ${PICO_BOARD}")
message(STATUS "║  Target: ${MIMIC_TARGET_RP2350} (0=RP2040, 1=RP2350)")
message(STATUS "║  Build:  ${CMAKE_BUILD_TYPE}")
message(STATUS "╚═══════════════════════════════════════════════════════════════╝")
message(STATUS "")
