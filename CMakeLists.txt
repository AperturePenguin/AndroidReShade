cmake_minimum_required(VERSION 3.22.1)
project(AndroidReShade CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# 编译选项
# ============================================================
add_compile_options(
    -Wall -Wextra
    -fvisibility=hidden
    -ffunction-sections
    -fdata-sections
    -O2
)

add_link_options(
    -Wl,--gc-sections
    -Wl,--strip-all
)

# ============================================================
# 公共 include 路径
# ============================================================
set(COMMON_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/common/include
    ${CMAKE_CURRENT_SOURCE_DIR}/app/src/main/cpp/third_party/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/app/src/main/cpp/third_party/bhook/include
)

# ============================================================
# Android 系统库
# ============================================================
find_library(log-lib  log)
find_library(android-lib android)
find_library(egl-lib  EGL)
find_library(gles3-lib GLESv3)

# ============================================================
# 模块 1：shader_loader
# ============================================================
add_library(shader_loader STATIC
    modules/shader_loader/src/shader_loader.cpp
)
target_include_directories(shader_loader PUBLIC
    modules/shader_loader/include
    ${COMMON_INCLUDES}
)
target_link_libraries(shader_loader ${log-lib})

# ============================================================
# 模块 2：hlsl_converter
# ============================================================
add_library(hlsl_converter STATIC
    modules/hlsl_converter/src/hlsl_converter.cpp
)
target_include_directories(hlsl_converter PUBLIC
    modules/hlsl_converter/include
    ${COMMON_INCLUDES}
)
target_link_libraries(hlsl_converter ${log-lib})

# ============================================================
# 模块 3：lut_loader
# ============================================================
add_library(lut_loader STATIC
    modules/lut_loader/src/lut_loader.cpp
)
target_include_directories(lut_loader PUBLIC
    modules/lut_loader/include
    ${COMMON_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}/app/src/main/cpp/third_party/stb
)
target_link_libraries(lut_loader ${log-lib} ${egl-lib} ${gles3-lib})

# ============================================================
# 模块 4：gpu_hook（包含 bhook 增强版 + Vulkan Layer）
# ============================================================
add_library(gpu_hook STATIC
    modules/gpu_hook/src/gpu_hook.cpp
    modules/gpu_hook/src/gpu_hook_bhook.cpp
)
target_include_directories(gpu_hook PUBLIC
    modules/gpu_hook/include
    ${COMMON_INCLUDES}
)
target_link_libraries(gpu_hook ${log-lib} ${egl-lib} ${gles3-lib})

# Vulkan Layer（单独动态库）
# 在 CI / 无 Vulkan SDK 的环境下自动跳过编译
option(BUILD_VULKAN_LAYER "Build VkLayerReShade (requires Vulkan SDK)" ON)
if(BUILD_VULKAN_LAYER)
    # 尝试用 find_library 找 vulkan，找不到则跳过
    find_library(vulkan-lib vulkan HINTS ${ANDROID_NDK} ENV VULKAN_SDK)
    if(vulkan-lib)
        add_library(VkLayerReShade SHARED
            modules/gpu_hook/src/vk_layer_reshade.cpp
        )
        target_include_directories(VkLayerReShade PRIVATE
            ${COMMON_INCLUDES}
        )
        target_link_libraries(VkLayerReShade ${log-lib} ${vulkan-lib})
    else()
        message(WARNING "Vulkan library not found, skipping VkLayerReShade build")
    endif()
else()
    message(STATUS "BUILD_VULKAN_LAYER is OFF, skipping VkLayerReShade")
endif()

# ============================================================
# 模块 5：shader_runtime
# ============================================================
add_library(shader_runtime STATIC
    modules/shader_runtime/src/shader_runtime.cpp
)
target_include_directories(shader_runtime PUBLIC
    modules/shader_runtime/include
    ${COMMON_INCLUDES}
)
target_link_libraries(shader_runtime
    shader_loader
    hlsl_converter
    lut_loader
    ${log-lib} ${egl-lib} ${gles3-lib}
)

# ============================================================
# 模块 7：config_system
# ============================================================
add_library(config_system STATIC
    modules/config_system/src/config_system.cpp
)
target_include_directories(config_system PUBLIC
    modules/config_system/include
    ${COMMON_INCLUDES}
)
target_link_libraries(config_system ${log-lib})

# ============================================================
# 核心注入动态库：libreshade_core.so
# ============================================================
add_library(reshade_core SHARED
    app/src/main/cpp/reshade_core.cpp
)
target_include_directories(reshade_core PRIVATE
    ${COMMON_INCLUDES}
    modules/shader_loader/include
    modules/hlsl_converter/include
    modules/lut_loader/include
    modules/gpu_hook/include
    modules/shader_runtime/include
    modules/config_system/include
)
target_link_libraries(reshade_core
    shader_loader
    hlsl_converter
    lut_loader
    gpu_hook
    shader_runtime
    config_system
    ${log-lib}
    ${android-lib}
    ${egl-lib}
    ${gles3-lib}
    dl
)
# 限制导出符号（减小库体积，降低被游戏检测的概率）
set_target_properties(reshade_core PROPERTIES
    LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/app/src/main/cpp/exports.map"
)

# ============================================================
# Zygisk 注入模块：libzygisk_reshade.so
# ============================================================
add_library(zygisk_reshade SHARED
    module/src/main/cpp/zygisk_module.cpp
)
target_include_directories(zygisk_reshade PRIVATE
    module/src/main/cpp/include
    ${COMMON_INCLUDES}
)
target_link_libraries(zygisk_reshade
    ${log-lib}
    dl
)

# ============================================================
# JNI Bridge（供 Kotlin 调用的 JNI 接口）
# ============================================================
add_library(reshade_jni SHARED
    app/src/main/cpp/reshade_jni.cpp
)
target_include_directories(reshade_jni PRIVATE
    ${COMMON_INCLUDES}
    modules/shader_loader/include
    modules/hlsl_converter/include
    modules/lut_loader/include
    modules/shader_runtime/include
    modules/config_system/include
)
target_link_libraries(reshade_jni
    reshade_core
    ${log-lib}
)
