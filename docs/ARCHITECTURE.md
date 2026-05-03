# Android ReShade — 系统架构文档

## 总体架构

```
┌─────────────────────────────────────────────────────────────────┐
│                        目标游戏进程                              │
│  ┌──────────────────┐    ┌────────────────────────────────────┐ │
│  │  游戏渲染调用     │───▶│  GLHook / VkHook (PLT/GOT 劫持)  │ │
│  │  eglSwapBuffers  │    │  拦截 SwapBuffers / Present        │ │
│  │  vkQueuePresent  │    └──────────────┬─────────────────────┘ │
│  └──────────────────┘                   │                        │
└─────────────────────────────────────────┼────────────────────────┘
                                          │ 注入层（Zygisk Module）
┌─────────────────────────────────────────▼────────────────────────┐
│                     libreshade_core.so                           │
│                                                                  │
│  ┌─────────────────┐  ┌──────────────────┐  ┌────────────────┐  │
│  │  ShaderCompiler │  │  PostProcessPipe │  │  LUTManager   │  │
│  │  HLSL/FX→GLSL   │  │  FBO链 + Pass调度 │  │  .png/.cube   │  │
│  │  SPIRV-Cross    │  │  GL/Vk统一接口   │  │  3D纹理上传   │  │
│  └────────┬────────┘  └────────┬─────────┘  └───────┬────────┘  │
│           └───────────────────▼──────────────────────┘           │
│                        EffectChain                               │
│                  (管理多 Pass 执行顺序)                           │
│  ┌──────────────────────────────────────────────────────────────┐│
│  │  Parameters: brightness / contrast / saturation / sharpen   ││
│  │  通过共享内存 / UNIX socket 与 UI 进程通信                    ││
│  └──────────────────────────────────────────────────────────────┘│
└──────────────────────────────────────────────────────────────────┘
                                          │ IPC (UNIX socket)
┌─────────────────────────────────────────▼────────────────────────┐
│                   ReShade 控制 App（独立进程）                    │
│  ┌───────────────────────────────────────────────────────────┐   │
│  │  悬浮窗 UI：SeekBar 调节 + Shader 列表 + LUT 选择         │   │
│  └───────────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────┘
```

## 关键技术路径

### 1. Root 注入方式
- **Zygisk Module**：在 zygote fork 时注入 `libreshade_core.so` 到目标游戏进程
- 备选：`/proc/<pid>/mem` 写入 + `ptrace` 手动注入（无需 Magisk）

### 2. Hook 策略
- **OpenGL ES**：PLT Hook（修改 `.so` 的 GOT 表）劫持 `eglSwapBuffers`
  - 使用 [bhook](https://github.com/bytedance/bhook) 或 [xhook](https://github.com/iqiyi/xHook)
- **Vulkan**：通过 Vulkan Layer 机制（`VK_LAYER_RESHADE`）劫持 `vkQueuePresentKHR`
  - Android 8.0+ 支持 implicit layer；可写入 `/data/local/debug/vulkan/`

### 3. Shader 转译
- **HLSL/FX → GLSL ES 3.0**：使用 [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross)
  - FX 文件先用 DXC/glslang 编译为 SPIR-V，再 cross-compile 到 GLSL
  - 简化路径：内置轻量 HLSL→GLSL 语义映射表（覆盖 ReShade 常用 intrinsics）

### 4. 后处理管线
- 在 `eglSwapBuffers` 调用前，将当前 FBO 内容 blit 到后处理链
- 执行 N 个 Pass（每个 Pass 对应一个 shader），最终输出到默认 FBO

### 5. LUT 支持
- `.png` → 解析为 512×512（或 1024×32 strip）的 3D LUT 纹理
- `.cube` → 直接解析 CUBE 文件格式，上传为 `GL_TEXTURE_3D`

### 6. UI 通信
- 控制 App 通过 UNIX domain socket 发送参数更新
- 注入库监听 socket，实时更新 uniform 值

## 文件结构

```
AndroidReShade/
├── app/                          # 控制面板 APK
│   └── src/main/
│       ├── java/com/reshade/android/
│       │   ├── MainActivity.kt          # 入口
│       │   ├── OverlayService.kt        # 悬浮窗 Service
│       │   ├── ShaderManagerActivity.kt # Shader 列表管理
│       │   └── IpcClient.kt             # UNIX socket 客户端
│       └── cpp/
│           ├── hook/
│           │   ├── gl_hook.cpp          # eglSwapBuffers 劫持
│           │   └── vk_hook.cpp          # Vulkan Layer 实现
│           ├── shader/
│           │   ├── fx_parser.cpp        # .fx 文件解析
│           │   ├── hlsl_transpiler.cpp  # HLSL→GLSL 转译
│           │   └── shader_cache.cpp     # 编译结果缓存
│           ├── pipeline/
│           │   ├── effect_chain.cpp     # Pass 链管理
│           │   ├── fbo_manager.cpp      # FBO 池
│           │   └── post_process.cpp     # 内置效果（亮度/对比度等）
│           └── lut/
│               ├── lut_loader.cpp       # .png / .cube 加载
│               └── lut_renderer.cpp     # LUT 应用
├── module/                        # Zygisk 注入模块
│   └── src/main/cpp/
│       └── zygisk_module.cpp
└── docs/
    └── ARCHITECTURE.md
```
