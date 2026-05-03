/**
 * Module 4: GpuHook — 实现
 *
 * ELF PLT/GOT Hook 原理：
 *   1. 扫描 /proc/self/maps 找到目标 .so 的基地址
 *   2. 解析 ELF PT_DYNAMIC 段，找到 DT_JMPREL (PLT重定位表)
 *   3. 对比符号名，找到 "eglSwapBuffers" 对应的 GOT slot
 *   4. mprotect 改页为可写，替换 GOT slot 为 hook 函数地址
 *   5. 保存原始地址供转发使用
 */
#include "gpu_hook.h"
#include <android/log.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <elf.h>
#include <link.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <mutex>

#define TAG "RS::GpuHook"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace reshade {

// ════════════════════════════════════════════════════════
// 全局状态
// ════════════════════════════════════════════════════════

using PFN_eglSwapBuffers = EGLBoolean(*)(EGLDisplay, EGLSurface);
using PFN_vkQueuePresent = VkResult (*)(VkQueue, const VkPresentInfoKHR*);

static PFN_eglSwapBuffers g_origSwap    = nullptr;
static PFN_vkQueuePresent g_origPresent = nullptr;

static GlPostProcessCb    g_glCb;
static VkPostProcessCb    g_vkCb;

static std::atomic<bool>  g_enabled{true};
static std::mutex         g_glMutex;
static std::mutex         g_vkMutex;

static std::atomic<int>   g_width{0};
static std::atomic<int>   g_height{0};

// ════════════════════════════════════════════════════════
// 公开接口
// ════════════════════════════════════════════════════════

bool GpuHook::installGL(GlPostProcessCb cb) {
    { std::lock_guard<std::mutex> lk(g_glMutex); g_glCb = std::move(cb); }

    // 先通过 dlsym 获得 fallback 原始指针
    void* eglLib = dlopen("libEGL.so", RTLD_NOLOAD | RTLD_LAZY);
    if (eglLib) {
        g_origSwap = (PFN_eglSwapBuffers)dlsym(eglLib, "eglSwapBuffers");
        dlclose(eglLib);
    }

    bool ok = scanAndHookGL();
    LOGI("installGL: %s (origSwap=%p)", ok ? "OK" : "PARTIAL", (void*)g_origSwap);
    return ok;
}

bool GpuHook::installVulkan(VkPostProcessCb cb) {
    { std::lock_guard<std::mutex> lk(g_vkMutex); g_vkCb = std::move(cb); }
    // Vulkan hook 通过 Layer 机制加载（见 vk_hook.cpp），此处仅保存回调
    LOGI("installVulkan: callback registered");
    return true;
}

void GpuHook::uninstallGL() {
    // 还原 GOT — 实际工程需要记录所有已 hook 的 slot
    std::lock_guard<std::mutex> lk(g_glMutex);
    g_glCb = nullptr;
    LOGI("uninstallGL");
}

void GpuHook::uninstallVulkan() {
    std::lock_guard<std::mutex> lk(g_vkMutex);
    g_vkCb = nullptr;
}

void GpuHook::setEnabled(bool on) { g_enabled.store(on); }
bool GpuHook::isEnabled()         { return g_enabled.load(); }
int  GpuHook::currentWidth()      { return g_width.load(); }
int  GpuHook::currentHeight()     { return g_height.load(); }

// ════════════════════════════════════════════════════════
// ELF GOT 替换工具
// ════════════════════════════════════════════════════════

bool GpuHook::replaceGOT(const char* libName,
                          const char* symbol,
                          void*       hookFn,
                          void**      origOut) {
    // ── 1. 从 /proc/self/maps 找到 so 基地址 ─────────────
    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps) return false;

    uintptr_t base = 0;
    char line[512];
    while (fgets(line, sizeof(line), maps)) {
        if (strstr(line, libName) && strstr(line, "r--p")) {
            sscanf(line, "%lx", &base);
            break;
        }
    }
    fclose(maps);
    if (!base) {
        LOGE("replaceGOT: '%s' not found in maps", libName);
        return false;
    }

    // ── 2. 解析 ELF 头 ────────────────────────────────────
    auto* ehdr = (ElfW(Ehdr)*)base;
    if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0) {
        LOGE("replaceGOT: invalid ELF magic in %s", libName);
        return false;
    }

    auto* phdr = (ElfW(Phdr)*)(base + ehdr->e_phoff);

    // ── 3. 找到 PT_DYNAMIC 段 ─────────────────────────────
    ElfW(Dyn)* dyn = nullptr;
    for (int i = 0; i < ehdr->e_phnum; ++i) {
        if (phdr[i].p_type == PT_DYNAMIC) {
            dyn = (ElfW(Dyn)*)(base + phdr[i].p_vaddr);
            break;
        }
    }
    if (!dyn) return false;

    // ── 4. 提取 PLT 重定位表、符号表、字符串表 ──────────
    ElfW(Rela)*  relTab   = nullptr;
    size_t       relCount = 0;
    const char*  strTab   = nullptr;
    ElfW(Sym)*   symTab   = nullptr;

    for (auto* d = dyn; d->d_tag != DT_NULL; ++d) {
        switch (d->d_tag) {
            case DT_JMPREL:   relTab   = (ElfW(Rela)*)(base + d->d_un.d_ptr); break;
            case DT_PLTRELSZ: relCount = d->d_un.d_val / sizeof(ElfW(Rela)); break;
            case DT_STRTAB:   strTab   = (const char*)(base + d->d_un.d_ptr); break;
            case DT_SYMTAB:   symTab   = (ElfW(Sym)* )(base + d->d_un.d_ptr); break;
            default: break;
        }
    }
    if (!relTab || !strTab || !symTab) return false;

    // ── 5. 遍历 PLT 重定位表，找目标符号 ─────────────────
    bool found = false;
    for (size_t i = 0; i < relCount; ++i) {
        uint32_t symIdx = ELF64_R_SYM(relTab[i].r_info);
        const char* symName = strTab + symTab[symIdx].st_name;

        if (strcmp(symName, symbol) != 0) continue;

        void** slot = (void**)(base + relTab[i].r_offset);
        uintptr_t page = (uintptr_t)slot & ~(uintptr_t)(getpagesize()-1);
        mprotect((void*)page, getpagesize(), PROT_READ | PROT_WRITE);

        if (origOut) *origOut = *slot;
        *slot = hookFn;

        mprotect((void*)page, getpagesize(), PROT_READ);  // 恢复只读
        __builtin___clear_cache((char*)slot, (char*)slot + sizeof(void*));

        LOGI("GOT patched: %s in %s  %p → %p",
             symbol, libName, origOut ? *origOut : nullptr, hookFn);
        found = true;
        // 不 break：同一 so 可能有多个重定位条目
    }
    return found;
}

// ════════════════════════════════════════════════════════
// 扫描所有已加载的 so，尝试 hook eglSwapBuffers
// ════════════════════════════════════════════════════════

bool GpuHook::scanAndHookGL() {
    // 常见游戏引擎 so 名（按概率排序）
    static const char* targets[] = {
        "libunity.so", "libUE4.so", "libUE5.so",
        "libcocos2djs.so", "libcocos2dx.so",
        "libgame.so", "libmain.so",
        "libEGL.so",   // fallback
        nullptr
    };

    bool any = false;
    for (int i = 0; targets[i]; ++i) {
        if (replaceGOT(targets[i], "eglSwapBuffers",
                       (void*)hookedEglSwapBuffers,
                       (void**)&g_origSwap))
            any = true;
    }
    return any;
}

// ════════════════════════════════════════════════════════
// Hook 函数实现
// ════════════════════════════════════════════════════════

EGLBoolean GpuHook::hookedEglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    if (!g_origSwap)
        return EGL_FALSE;

    if (g_enabled.load()) {
        // 更新尺寸
        int w = 0, h = 0;
        eglQuerySurface(dpy, surface, EGL_WIDTH,  &w);
        eglQuerySurface(dpy, surface, EGL_HEIGHT, &h);
        if (w > 0) g_width .store(w);
        if (h > 0) g_height.store(h);

        // 保存 GL 状态
        GLint prevFBO = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
        GLint vp[4];
        glGetIntegerv(GL_VIEWPORT, vp);
        GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
        GLboolean blend     = glIsEnabled(GL_BLEND);

        // 触发后处理回调
        GlPostProcessCb cb;
        { std::lock_guard<std::mutex> lk(g_glMutex); cb = g_glCb; }
        if (cb) cb(prevFBO, g_width.load(), g_height.load());

        // 恢复 GL 状态
        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
        glViewport(vp[0], vp[1], vp[2], vp[3]);
        if (!depthTest) glDisable(GL_DEPTH_TEST);
        if (!blend)     glDisable(GL_BLEND);
    }

    return g_origSwap(dpy, surface);
}

VkResult GpuHook::hookedVkQueuePresent(VkQueue q, const VkPresentInfoKHR* p) {
    if (g_enabled.load()) {
        VkPostProcessCb cb;
        { std::lock_guard<std::mutex> lk(g_vkMutex); cb = g_vkCb; }
        if (cb) cb(q, p);
    }
    return g_origPresent ? g_origPresent(q, p) : VK_ERROR_DEVICE_LOST;
}

} // namespace reshade
