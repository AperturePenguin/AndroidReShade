# AndroidReShade — 完整使用说明

> Android Root 平台实时游戏滤镜系统，类 PC ReShade
> 
> 版本：v1.0.0 | 支持：Android 9+ / OpenGL ES 3.0+ / Vulkan 1.1+

---

## 目录

1. [系统要求](#系统要求)
2. [项目结构](#项目结构)
3. [编译构建](#编译构建)
4. [部署安装](#部署安装)
5. [配置目标游戏](#配置目标游戏)
6. [使用 Shader](#使用-shader)
7. [使用 LUT](#使用-lut)
8. [参数调节 UI](#参数调节-ui)
9. [Preset 管理](#preset-管理)
10. [性能调优](#性能调优)
11. [常见问题 FAQ](#常见问题-faq)
12. [开发调试](#开发调试)

---

## 系统要求

| 要求 | 最低 | 推荐 |
|------|------|------|
| Android 版本 | 9.0 (API 28) | 12+ (API 31+) |
| Root 框架 | Magisk 24.0 | Magisk 26.0 + KernelSU |
| Zygisk | 必须启用 | — |
| GPU API | OpenGL ES 3.0 | OpenGL ES 3.2 / Vulkan 1.1 |
| 架构 | arm64-v8a | — |

---

## 项目结构

```
AndroidReShade/
├── app/                            ← 控制台 APK 源码
│   └── src/main/
│       ├── cpp/
│       │   ├── reshade_core.cpp    ← 注入库主入口
│       │   ├── reshade_jni.cpp     ← JNI Bridge（Kotlin ↔ C++）
│       │   └── third_party/
│       │       ├── bhook/          ← ByteDance PLT Hook（需手动下载）
│       │       └── stb/            ← stb_image（需手动下载）
│       └── java/com/reshade/android/
│           ├── jni/ReShadeBridge.kt  ← JNI 桥接对象
│           ├── ui/ControlPanel.kt    ← 动态 Slider UI
│           ├── config/ConfigManager.kt
│           └── ipc/IpcClient.kt
│
├── modules/                        ← 7 个 C++ 模块
│   ├── shader_loader/              ← .fx/.hlsl 解析
│   ├── hlsl_converter/             ← HLSL→GLSL ES 转译
│   ├── lut_loader/                 ← .cube/.png LUT 加载
│   ├── gpu_hook/                   ← PLT Hook + Vulkan Layer
│   │   ├── gpu_hook_bhook.cpp      ← bhook 增强版
│   │   └── vk_layer_reshade.cpp    ← Vulkan Layer
│   ├── shader_runtime/             ← FBO Pipeline 管理
│   ├── config_system/              ← INI Preset 系统
│   └── common/                     ← 公共头文件
│
├── module/                         ← Zygisk 注入模块
│   └── src/main/cpp/zygisk_module.cpp
│
├── magisk_module/                  ← Magisk 模块打包框架
│   ├── module.prop
│   ├── customize.sh                ← 安装脚本
│   └── service.sh                  ← 开机服务
│
├── assets/
│   ├── shaders/                    ← 示例 Shader 文件
│   │   ├── ColorCorrection.fx      ← 色彩校正（亮度/对比度/饱和度/Gamma）
│   │   ├── Sharpen.fx              ← 自适应锐化（Unsharp Mask）
│   │   ├── ShadowLift.fx           ← 暗部增强 + 色调偏移
│   │   ├── LUTMapping.fx           ← 3D LUT 颜色映射
│   │   └── Vignette.fx             ← 暗角效果
│   └── luts/                       ← 示例 LUT 文件
│       ├── identity.cube           ← 恒等变换（测试用）
│       ├── warm_tone.cube          ← 暖橙/黄调
│       ├── cool_tone.cube          ← 冷蓝调
│       ├── film_look.cube          ← 胶片感 S 曲线
│       └── teal_orange.cube        ← 电影青橙调
│
├── CMakeLists.txt                  ← 顶层 CMake（7 模块 + 2 动态库 + JNI）
└── scripts/
    ├── build_magisk_module.sh      ← Magisk ZIP 打包脚本
    └── gen_luts.py                 ← LUT 生成脚本
```

---

## 编译构建

### 1. 前置依赖

```bash
# 1.1 下载 stb_image（单头文件）
curl -o app/src/main/cpp/third_party/stb/stb_image.h \
  https://raw.githubusercontent.com/nothings/stb/master/stb_image.h

# 1.2 下载 zygisk.hpp
curl -o module/src/main/cpp/include/zygisk.hpp \
  https://raw.githubusercontent.com/topjohnwu/zygisk-module-sample/master/module/jni/zygisk.hpp

# 1.3 下载 bhook（推荐使用真实库提高 Hook 兼容性）
# 从 https://github.com/bytedance/bhook/releases 下载最新 release
# 解压后将 include/bytehook.h 放入：
#   app/src/main/cpp/third_party/bhook/include/
# 将 libs/arm64-v8a/libbytehook.so 放入：
#   app/src/main/cpp/third_party/bhook/lib/arm64-v8a/
# 注意：若不下载，系统将使用内置 stub（hook 功能不生效）
```

### 2. 编译 APK

```bash
cd AndroidReShade

# Debug 版本
./gradlew assembleDebug

# Release 版本
./gradlew assembleRelease

# 输出文件：
# app/build/outputs/apk/debug/app-debug.apk
# app/build/outputs/apk/release/app-release.apk
```

### 3. 打包 Magisk 模块

```bash
# 先编译 Release APK，再打包模块
./gradlew assembleRelease
bash scripts/build_magisk_module.sh release

# 输出：dist/AndroidReShade_v1.0.0_magisk.zip
```

---

## 部署安装

### 方式一：Magisk Module（推荐）

```bash
# 推送到手机
adb push dist/AndroidReShade_v1.0.0_magisk.zip /sdcard/

# 在手机上：Magisk Manager → 模块 → 从本地安装 → 选择 ZIP
# 安装完成后重启设备
```

### 方式二：手动部署（调试用）

```bash
# 安装控制台 APK
adb install -r app/build/outputs/apk/debug/app-debug.apk

# 推送注入库
adb shell mkdir -p /data/local/tmp
adb push app/build/intermediates/cmake/debug/obj/arm64-v8a/libreshade_core.so \
         /data/local/tmp/libreshade_core.so
adb shell chmod 755 /data/local/tmp/libreshade_core.so

# 推送 Zygisk 模块（需要 Magisk 已启用 Zygisk）
adb push app/build/intermediates/cmake/debug/obj/arm64-v8a/libzygisk_reshade.so \
         /data/adb/modules/android_reshade/zygisk/arm64-v8a.so
```

### 方式三：Vulkan Layer（仅 Vulkan 游戏）

```bash
# Android 10+（仅调试模式有效，或通过 Zygisk 注入）
adb shell mkdir -p /data/local/debug/vulkan
adb push app/build/intermediates/cmake/debug/obj/arm64-v8a/libVkLayerReShade.so \
         /data/local/debug/vulkan/
adb shell chmod 644 /data/local/debug/vulkan/libVkLayerReShade.so

# 对目标应用开启 Vulkan Layer（需要 root）
adb shell setprop debug.vulkan.layers VK_LAYER_RESHADE_android
```

---

## 配置目标游戏

### 方式一：通过 APK 界面配置

1. 打开 **AndroidReShade** 应用
2. 点击 "+" 添加游戏
3. 从已安装应用列表中选择目标游戏
4. 点击保存

### 方式二：手动编辑配置文件

```bash
# 编辑 /sdcard/ReShade/reshade.ini
adb shell

cat >> /sdcard/ReShade/reshade.ini << 'EOF'
[Targets]
PackageName=com.tencent.tmgp.pubgmhd
PackageName=com.netease.x19
EOF
```

---

## 使用 Shader

### 放置 Shader 文件

```bash
# 将 .fx 或 .hlsl 文件推送到：
adb push YourShader.fx /sdcard/ReShade/shaders/

# 内置示例 shader 位于（APK 安装时自动部署）：
# /sdcard/ReShade/shaders/ColorCorrection.fx   ← 色彩校正
# /sdcard/ReShade/shaders/Sharpen.fx           ← 锐化
# /sdcard/ReShade/shaders/ShadowLift.fx        ← 暗部增强
# /sdcard/ReShade/shaders/LUTMapping.fx        ← LUT 映射
# /sdcard/ReShade/shaders/Vignette.fx          ← 暗角
```

### 在应用中激活

1. 打开 AndroidReShade 应用
2. 切换到 "Shaders" 标签
3. 点击 Shader 名称左侧复选框启用
4. 长按可调整执行顺序
5. 点击 Shader 名称展开参数 Slider

### 支持的 Shader 格式

| 格式 | 说明 | 兼容性 |
|------|------|--------|
| ReShade 4.x `.fx` | 完整 technique/pass 支持 | ✅ |
| SweetFX `.fx` | 基础效果链 | ✅ |
| 纯 `.hlsl` | 需指定入口函数 | ✅ |
| `.glsl` | 直接使用，无需转译 | ✅ |

> ⚠️ ReShade 5.x 的 Addon API、compute shader、ray tracing 不支持。

---

## 使用 LUT

### 放置 LUT 文件

```bash
# .cube 格式（推荐，支持任意尺寸）
adb push YourLUT.cube /sdcard/ReShade/luts/

# .png Hald CLUT（8x8x8 = 512x512，或 16x16x16 = 4096x512）
adb push YourHaldCLUT.png /sdcard/ReShade/luts/
```

### 内置 LUT 预设

| 文件名 | 效果 |
|--------|------|
| `identity.cube` | 无变化（测试用） |
| `warm_tone.cube` | 暖橙/黄色调 |
| `cool_tone.cube` | 冷蓝调 |
| `film_look.cube` | 胶片感 S 曲线对比度 |
| `teal_orange.cube` | 电影青橙调（推荐） |

### 激活 LUT

1. 在 AndroidReShade 应用中启用 `LUTMapping.fx`
2. 在 "LUTs" 标签中选择 .cube 文件
3. 调节 "LUT 强度" Slider（0.0~1.0）

---

## 参数调节 UI

### 悬浮窗面板

游戏运行时，系统托盘会出现悬浮窗图标（可拖动）：

| 参数 | 范围 | 默认 | Shader |
|------|------|------|--------|
| 亮度 | -1.0 ~ +1.0 | 0.0 | ColorCorrection |
| 对比度 | 0.0 ~ 3.0 | 1.0 | ColorCorrection |
| 饱和度 | 0.0 ~ 3.0 | 1.0 | ColorCorrection |
| Gamma | 0.1 ~ 3.0 | 1.0 | ColorCorrection |
| 锐化强度 | 0.0 ~ 5.0 | 1.5 | Sharpen |
| 锐化半径 | 0.5 ~ 3.0 | 1.0 | Sharpen |
| 暗部提升 | 0.0 ~ 1.0 | 0.2 | ShadowLift |
| LUT 强度 | 0.0 ~ 1.0 | 1.0 | LUTMapping |
| 暗角强度 | 0.0 ~ 1.0 | 0.6 | Vignette |

---

## Preset 管理

### 保存当前配置

```
在悬浮窗顶部 → "保存 Preset" → 输入名称
```

### 切换 Preset

```
悬浮窗顶部下拉菜单 → 选择 Preset 名称
```

### 手动编辑 Preset

```ini
# /sdcard/ReShade/presets/MyPreset.ini
[ColorCorrection]
Brightness=0.05
Contrast=1.15
Saturation=1.30
Gamma=0.95

[Sharpen]
SharpenStrength=2.0
SharpenRadius=1.0

[ShadowLift]
ShadowLiftAmount=0.25
ShadowGamma=0.75
```

---

## 性能调优

### 推荐设置（高帧率游戏）

```ini
# /sdcard/ReShade/reshade.ini
[Performance]
; 限制同时启用的 Pass 数（建议 ≤ 3）
MaxPasses=3

; 降低 LUT 分辨率（8³ 代替 32³）节省内存
LUTSize=8
```

### GPU 开销估算

| 配置 | 额外 GPU 耗时（1080p/60fps） |
|------|------------------------------|
| 仅 ColorCorrection | ~0.3ms |
| ColorCorrection + Sharpen | ~0.6ms |
| + LUT (32³) | ~0.9ms |
| 完整 5 个效果 | ~1.5ms |

### 降低开销的技巧

1. **关闭不需要的效果**：每个 Pass 都有固定开销
2. **降低 LUT 尺寸**：8³ 效果与 32³ 肉眼基本无差异
3. **关闭 Draw Call Hook**：仅保留 SwapBuffers Hook
4. **ShadowLift 最耗 GPU**：Sharpen 其次

---

## 常见问题 FAQ

### Q1: 安装后游戏没有效果？

1. 确认 Zygisk 已启用（Magisk 设置 → Zygisk → 已启用）
2. 确认目标游戏包名已在 `reshade.ini` 的 `[Targets]` 中配置
3. 重启设备后再测试
4. 查看日志：`adb logcat -s "AndroidReShade"`

### Q2: 游戏闪退 / 崩溃？

1. 部分游戏检测到 Hook 会主动退出，尝试配合 **Shamiko**（Zygisk 反检测模块）
2. 检查 Shader 文件是否有语法错误（日志中查找 `COMPILE ERROR`）
3. 尝试关闭所有 Shader 后逐一启用，定位问题 Shader

### Q3: HLSL Shader 转译失败？

常见问题与修复：
```hlsl
// ❌ ReShade 5.x 专有宏
#ifdef __RESHADE__

// ✅ 替换为条件预编译
#ifdef ANDROID_RESHADE
```

```hlsl
// ❌ cbuffer（不支持）
cbuffer Params : register(b0) { float brightness; }

// ✅ 改为 uniform
uniform float brightness;
```

### Q4: Vulkan 游戏无效果？

- 确认 Android 版本 ≥ 10（API 29+）
- 通过 Zygisk 注入：Layer 必须通过模块部署，不能只放到 `/data/local/debug/vulkan/`
- 部分厂商 ROM（小米 MIUI、OPPO ColorOS）禁用了 Vulkan Layer，暂无解法

### Q5: 悬浮窗无法显示？

需要授予悬浮窗权限：
```
设置 → 应用 → AndroidReShade → 显示在其他应用上层 → 允许
```

或通过 ADB：
```bash
adb shell appops set com.reshade.android SYSTEM_ALERT_WINDOW allow
```

### Q6: 支持哪些游戏？

理论上支持所有使用 OpenGL ES 的 Android 游戏。已测试：
- ✅ PUBG Mobile / 和平精英
- ✅ 原神（OpenGL ES 模式）
- ✅ 王者荣耀
- ⚠️ 原神 Vulkan 模式（Vulkan Layer 实验性支持）
- ❌ 带强力反作弊的游戏（如 BattlEye 手游版）

---

## 开发调试

### 查看实时日志

```bash
# 过滤 AndroidReShade 日志
adb logcat -s "AndroidReShade" -s "AndroidReShade/JNI" -s "AndroidReShade/BHook" -s "AndroidReShade/VkLayer"

# 保存到文件
adb logcat -s "AndroidReShade*" > reshade_debug.log
```

### 调试 HLSL 转译

```bash
# 在 adb shell 中查看转译结果
adb logcat -s "AndroidReShade/HlslConv"
# 搜索 "GLSL output:" 可看到转译后的 GLSL 源码
```

### 性能分析

```bash
# 查看帧耗时
adb logcat -s "AndroidReShade/Runtime" | grep "frame_time"
```

### 添加自定义 Shader

参考 `assets/shaders/ColorCorrection.fx`，编写 ReShade 4.x 格式的 `.fx` 文件，放到 `/sdcard/ReShade/shaders/` 即可自动识别。

---

## 第三方依赖

| 库 | 用途 | 许可证 |
|----|------|--------|
| [stb_image](https://github.com/nothings/stb) | PNG LUT 解码 | MIT / Public Domain |
| [zygisk.hpp](https://github.com/topjohnwu/zygisk-module-sample) | Zygisk API | MIT |
| [bhook](https://github.com/bytedance/bhook) | PLT Hook 增强 | MIT |
| [Vulkan SDK](https://vulkan.lunarg.com/) | Vulkan Layer API | Apache 2.0 |

---

## License

MIT License — 开源免费，禁止用于破坏游戏公平性（外挂）等违规用途。
