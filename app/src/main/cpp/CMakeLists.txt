cmake_minimum_required(VERSION 3.22.1)
project(reshade_core VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ──────────────────────────────────────────────────────────
# 编译选项
# ──────────────────────────────────────────────────────────
add_compile_options(
    -Wall
    -Wno-unused-variable
    -fvisibility=hidden        # 隐藏所有符号（只导出显式标记的）
    -fstack-protector-strong
    -O2
)

# ──────────────────────────────────────────────────────────
# 头文件路径
# ──────────────────────────────────────────────────────────
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/hook
    ${CMAKE_CURRENT_SOURCE_DIR}/shader
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline
    ${CMAKE_CURRENT_SOURCE_DIR}/lut
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zygisk
)

# ──────────────────────────────────────────────────────────
# 核心注入库（libreshade_core.so）
# ──────────────────────────────────────────────────────────
add_library(
    reshade_core
    SHARED
    reshade_core.cpp
    hook/gl_hook.cpp
    hook/vk_hook.cpp
    shader/fx_parser.cpp
    shader/hlsl_transpiler.cpp
    pipeline/effect_chain.cpp
    lut/lut_loader.cpp
)

target_link_libraries(
    reshade_core
    android
    log
    EGL
    GLESv3
    vulkan
    dl
    pthread
)

# 确保符号只导出 reshade_inject_init
target_link_options(reshade_core PRIVATE
    -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map
)

# ──────────────────────────────────────────────────────────
# Vulkan Layer（libVkLayer_reshade.so）
# ──────────────────────────────────────────────────────────
add_library(
    VkLayer_reshade
    SHARED
    hook/vk_hook.cpp
)

target_link_libraries(VkLayer_reshade log vulkan dl pthread)

# ──────────────────────────────────────────────────────────
# Zygisk 模块（libzygisk_module.so）
# ──────────────────────────────────────────────────────────
# 注：Zygisk 模块通常在独立的 module/ 目录下构建
# 这里仅供参考，实际需要 Zygisk API 头文件
