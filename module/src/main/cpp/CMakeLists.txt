cmake_minimum_required(VERSION 3.22.1)
project(zygisk_reshade)

set(CMAKE_CXX_STANDARD 17)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(
    zygisk_reshade
    SHARED
    zygisk_module.cpp
)

target_link_libraries(zygisk_reshade log dl)

# Zygisk 模块 .so 输出
set_target_properties(zygisk_reshade PROPERTIES
    OUTPUT_NAME "zygisk_reshade"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/libs/${ANDROID_ABI}"
)
