cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME smart_home)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    ${CMAKE_CURRENT_SOURCE_DIR}/APP/esp32.c
    ${CMAKE_CURRENT_SOURCE_DIR}/APP/schedule.c
    ${CMAKE_CURRENT_SOURCE_DIR}/APP/my_uart.c
    ${CMAKE_CURRENT_SOURCE_DIR}/APP/face.c
    ${CMAKE_CURRENT_SOURCE_DIR}/APP/smoke.c
    ${CMAKE_CURRENT_SOURCE_DIR}/APP/json_parser.c
    ${CMAKE_CURRENT_SOURCE_DIR}/APP/my_adc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/APP/PM25.c
    ${CMAKE_CURRENT_SOURCE_DIR}/APP/BH1750.c
    ${CMAKE_CURRENT_SOURCE_DIR}/APP/voice.c
    ${CMAKE_CURRENT_SOURCE_DIR}/APP/lcd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/APP/DHT11.c
    ${CMAKE_CURRENT_SOURCE_DIR}/APP/fan.c
    ${CMAKE_CURRENT_SOURCE_DIR}/APP/ws2812.c
    ${CMAKE_CURRENT_SOURCE_DIR}/APP/ws2812_2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/APP/ws2812_3.c
    ${CMAKE_CURRENT_SOURCE_DIR}/APP/config_store.c
    ${CMAKE_CURRENT_SOURCE_DIR}/APP/device_state.c
    ${CMAKE_CURRENT_SOURCE_DIR}/APP/automation.c
    ${CMAKE_CURRENT_SOURCE_DIR}/APP/health_monitor.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    ${CMAKE_CURRENT_SOURCE_DIR}/APP
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
# --- 添加自定义命令，用于生成 HEX 和 BIN 文件 ---
# 设置生成文件的路径和名称
set(HEX_FILE ${PROJECT_BINARY_DIR}/${PROJECT_NAME}.hex)
set(BIN_FILE ${PROJECT_BINARY_DIR}/${PROJECT_NAME}.bin)

# 为项目的主要目标添加一个编译后（POST_BUILD）的自定义命令
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    # 使用 objcopy 工具将主目标文件（通常是 .elf）转换为 Intel HEX 格式
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${PROJECT_NAME}> ${HEX_FILE}
    # 使用 objcopy 工具将主目标文件转换为纯二进制格式
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}> ${BIN_FILE}
    # 在编译输出中显示提示信息
    COMMENT "Generating HEX and BIN files from ${PROJECT_NAME}"
)
