#pragma once
/**
 * Module 4: GpuHook — GPU 渲染拦截核心
 * ────────────────────────────────────────
 * 职责：
 *   - Hook eglSwapBuffers（OpenGL ES 路径）
 *   - Hook vkQueuePresentKHR（Vulkan 路径）
 *   - 在最终帧输出前插入后处理回调
 *
 * 设计原则：
 *   - 与 ShaderRuntime 完全解耦，通过回调注入
 *   - 线程安全（GL 在渲染线程回调；Vk 同理）
 *   - 状态最小化，只保存 hook 前后的函数指针
 */
#include <functional>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <vulkan/vulkan.h>

namespace reshade {

// ════════════════════════════════════════════════════════
// 回调类型
// ════════════════════════════════════════════════════════

/**
 * GL 后处理回调
 * @param currentFBO  当前绑定的 FBO（游戏的 default framebuffer id）
 * @param width       surface 宽度
 * @param height      surface 高度
 */
using GlPostProcessCb = std::function<void(GLint currentFBO, int width, int height)>;

/**
 * Vulkan 后处理回调
 * @param queue         present 队列
 * @param pPresentInfo  原始 present 信息（包含 swapchain image）
 */
using VkPostProcessCb = std::function<void(VkQueue queue,
                                            const VkPresentInfoKHR* pPresentInfo)>;

// ════════════════════════════════════════════════════════
// GpuHook 接口
// ════════════════════════════════════════════════════════

class GpuHook {
public:
    // ── 安装 Hook ────────────────────────────────────────

    /**
     * 安装 OpenGL ES Hook（PLT/GOT 劫持 eglSwapBuffers）
     * 需要在目标进程注入后、第一次 GL 调用之前调用。
     * @param cb  每帧在 SwapBuffers 前触发的回调
     */
    static bool installGL(GlPostProcessCb cb);

    /**
     * 安装 Vulkan Layer Hook（需提前作为 Layer 加载，或通过 PLT Hook）
     * @param cb  每次 QueuePresent 前触发的回调
     */
    static bool installVulkan(VkPostProcessCb cb);

    // ── 卸载 ─────────────────────────────────────────────

    static void uninstallGL();
    static void uninstallVulkan();

    // ── 开关 ─────────────────────────────────────────────

    static void setEnabled(bool on);
    static bool isEnabled();

    // ── 当前帧尺寸查询 ────────────────────────────────────

    static int  currentWidth();
    static int  currentHeight();

private:
    // PLT hook 实现
    static bool pltHookGL   (const char* libName);
    static bool scanAndHookGL();

    // hook 函数（静态，供 PLT 替换用）
    static EGLBoolean hookedEglSwapBuffers(EGLDisplay dpy, EGLSurface surface);
    static VkResult   hookedVkQueuePresent(VkQueue q, const VkPresentInfoKHR* p);

    // 内部工具：修改 ELF GOT 表中的函数指针
    static bool replaceGOT(const char* libName,
                            const char* symbol,
                            void*       hookFn,
                            void**      origOut);
};

} // namespace reshade
