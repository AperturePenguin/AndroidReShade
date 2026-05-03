# AndroidReShade

> Android（Root）平台的实时游戏滤镜系统，类 PC ReShade
>
> 直接加载 `.fx` / `.hlsl` / `.cube` / `.png` 文件，在 OpenGL ES / Vulkan 游戏中实现实时后处理

---

## 功能特性

| 功能 | 状态 |
|------|------|
| **eglSwapBuffers 拦截**（OpenGL ES） | ✅ |
| **Vulkan Layer**（vkQueuePresentKHR） | ✅ |
| **Zygisk 进程注入** | ✅ |
| **bhook PLT Hook 增强**（ByteDance） | ✅ |
| **.fx / .hlsl 文件解析**（technique/pass/uniform） | ✅ |
| **HLSL → GLSL ES 3.0 转译** | ✅ |
| **FBO 后处理链**（多 Pass 有序执行） | ✅ |
| **.cube LUT 加载**（三线性重采样 → 3D 纹理） | ✅ |
| **.png Hald CLUT 加载** | ✅ |
| **悬浮窗实时参数调节 UI**（动态 Slider） | ✅ |
| **UNIX socket IPC 通信** | ✅ |
| **JNI Bridge**（Kotlin ↔ C++ 双向调用） | ✅ |
| **INI Preset 系统**（保存/加载效果预设） | ✅ |

---

## 示例效果

| Shader | 效果说明 |
|--------|----------|
| `ColorCorrection.fx` | 亮度 / 对比度 / 饱和度 / Gamma 四合一 |
| `Sharpen.fx` | Unsharp Mask 自适应锐化 |
| `ShadowLift.fx` | 暗部非线性提亮 + 电影色调偏移 |
| `LUTMapping.fx` | 3D LUT 颜色映射（.cube / .png） |
| `Vignette.fx` | 暗角 / 渐晕效果 |

| LUT | 效果说明 |
|-----|----------|
| `warm_tone.cube` | 暖橙/黄调 |
| `cool_tone.cube` | 冷蓝调 |
| `film_look.cube` | 胶片感 S 曲线对比度 |
| `teal_orange.cube` | 电影青橙调（Teal & Orange） |

---

## 系统架构

```
游戏进程
  └── eglSwapBuffers / vkQueuePresentKHR  ← Hook 点（bhook PLT + Vulkan Layer）
            │
     libreshade_core.so（Zygisk 注入）
            │
   ┌────────┴──────────────────────────┐
   │        ShaderRuntime               │
   │  ┌────────────────────────────┐   │
   │  │  Effect 1: ColorCorrection  │   │
   │  │  Effect 2: ShadowLift       │   │  ← .fx 文件 → HLSL 转 GLSL → 编译
   │  │  Effect 3: Sharpen          │   │
   │  │  Effect 4: LUTMapping       │   │  ← .cube/.png → GL_TEXTURE_3D
   │  │  Effect 5: Vignette         │   │
   │  └────────────────────────────┘   │
   └───────────────────────────────────┘
            │  JNI + UNIX socket
   AndroidReShade APK
            │
   悬浮窗 ControlPanel（动态 Slider）
            │
   ConfigManager（INI Preset 保存/加载）
```

---

## 快速开始

### 1. 编译

```bash
# 下载依赖
curl -o app/src/main/cpp/third_party/stb/stb_image.h \
  https://raw.githubusercontent.com/nothings/stb/master/stb_image.h

curl -o module/src/main/cpp/include/zygisk.hpp \
  https://raw.githubusercontent.com/topjohnwu/zygisk-module-sample/master/module/jni/zygisk.hpp

# 编译 APK
./gradlew assembleRelease

# 打包 Magisk 模块
bash scripts/build_magisk_module.sh release
```

### 2. 安装

```bash
# 推送 Magisk 模块 ZIP 到手机后，在 Magisk Manager 中安装并重启
adb push dist/AndroidReShade_v1.0.0_magisk.zip /sdcard/

# 安装控制台 APK
adb install app/build/outputs/apk/release/app-release.apk
```

### 3. 配置目标游戏

```bash
adb shell "cat >> /sdcard/ReShade/reshade.ini << 'EOF'
[Targets]
PackageName=com.your.target.game
EOF"
```

### 4. 放置 Shader 文件

```bash
# 内置示例 Shader 在安装时自动部署到 /sdcard/ReShade/shaders/
# 也可手动推送 PC ReShade shader：
adb push YourShader.fx /sdcard/ReShade/shaders/
adb push YourLUT.cube  /sdcard/ReShade/luts/
```

### 5. 启动游戏，打开悬浮窗调节参数

---

## 模块说明

| 模块 | 路径 | 功能 |
|------|------|------|
| **ShaderLoader** | `modules/shader_loader/` | .fx/.hlsl 解析，提取 uniform/pass/technique |
| **HlslConverter** | `modules/hlsl_converter/` | HLSL → GLSL ES 三层转换 |
| **LutLoader** | `modules/lut_loader/` | .cube/.png → GL_TEXTURE_3D |
| **GpuHook** | `modules/gpu_hook/` | bhook PLT Hook + Vulkan Layer |
| **ShaderRuntime** | `modules/shader_runtime/` | FBO 双缓冲 + 多 Pass 执行 |
| **ConfigSystem** | `modules/config_system/` | INI Preset 序列化/反序列化 |
| **ControlPanel** | `app/.../ui/ControlPanel.kt` | 动态 Slider UI，按 uniform 自动生成 |

---

## 系统要求

- Android 9.0+ (API 28+)
- Magisk 24.0+ + **Zygisk 启用**
- OpenGL ES 3.0+ 或 Vulkan 1.1+
- arm64-v8a 架构

---

## 第三方依赖（需手动添加）

| 库 | 放置路径 | 获取地址 |
|----|----------|----------|
| `stb_image.h` | `app/src/main/cpp/third_party/stb/` | https://github.com/nothings/stb |
| `zygisk.hpp` | `module/src/main/cpp/include/` | https://github.com/topjohnwu/zygisk-module-sample |
| `bhook`（可选） | `app/src/main/cpp/third_party/bhook/` | https://github.com/bytedance/bhook |

---

## 详细文档

- 📖 [完整使用说明](docs/USAGE.md)
- 🏗️ [系统架构设计](docs/ARCHITECTURE.md)

---

## License

MIT — 开源免费，禁止用于破坏游戏公平性等违规用途
