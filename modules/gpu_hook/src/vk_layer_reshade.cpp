/**
 * vk_layer_reshade.cpp
 * AndroidReShade Vulkan Layer
 *
 * 拦截 vkQueuePresentKHR，在呈现前执行后处理。
 * 实现为标准 Vulkan Explicit Layer，无需游戏改动。
 *
 * 部署方式：
 *   - Android 10+：放到 /data/local/debug/vulkan/ 即可加载（调试模式）
 *   - Android 9 ：需要游戏开启 debuggable 或通过 Zygisk 注入
 *
 * 参考：
 *   https://vulkan.lunarg.com/doc/view/latest/linux/layer_configuration.html
 *   https://github.com/KhronosGroup/Vulkan-Loader
 */

#include <vulkan/vulkan.h>
#include <android/log.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <cstring>

#define TAG "AndroidReShade/VkLayer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// ============================================================
// Layer 元信息
// ============================================================
static const char* LAYER_NAME    = "VK_LAYER_RESHADE_android";
static const char* LAYER_DESC    = "AndroidReShade post-processing layer";
static const uint32_t LAYER_SPEC = VK_API_VERSION_1_1;
static const uint32_t LAYER_IMPL = 1;

// ============================================================
// 每个 VkDevice 的状态
// ============================================================
struct DeviceData {
    VkDevice                     device      = VK_NULL_HANDLE;
    VkPhysicalDevice             physDevice  = VK_NULL_HANDLE;
    PFN_vkGetDeviceProcAddr      gdpa        = nullptr;

    // 原始 vkQueuePresentKHR
    PFN_vkQueuePresentKHR        fpQueuePresent = nullptr;

    // 后处理资源（简化：仅拷贝 image 到离屏 + 执行 compute/blit）
    VkCommandPool                cmdPool     = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> cmdBufs;

    // 设备函数表（常用）
    PFN_vkCreateCommandPool      fpCreateCommandPool  = nullptr;
    PFN_vkAllocateCommandBuffers fpAllocCmdBufs       = nullptr;
    PFN_vkBeginCommandBuffer     fpBeginCmdBuf        = nullptr;
    PFN_vkEndCommandBuffer       fpEndCmdBuf          = nullptr;
    PFN_vkCmdBlitImage           fpCmdBlitImage       = nullptr;
    PFN_vkQueueSubmit            fpQueueSubmit        = nullptr;
    PFN_vkDestroyCommandPool     fpDestroyCommandPool = nullptr;

    bool initialized = false;
};

struct InstanceData {
    VkInstance                    instance    = VK_NULL_HANDLE;
    PFN_vkGetInstanceProcAddr     gipa        = nullptr;
    PFN_vkGetPhysicalDeviceProperties fpGetPhysDevProps = nullptr;
};

// ============================================================
// 全局表（key = dispatch table pointer，即 VkDevice/VkInstance 首地址中的指针）
// ============================================================
static std::mutex                                     g_mutex;
static std::unordered_map<void*, DeviceData>          g_devices;
static std::unordered_map<void*, InstanceData>        g_instances;

// 取 dispatch key（Vulkan loader 约定：对象首个字段是 loader dispatch 指针）
static inline void* GetKey(const void* object)
{
    return *reinterpret_cast<void* const*>(object);
}

// ============================================================
// 帮助：加载设备函数
// ============================================================
#define LOAD_DEV_FN(dd, name) \
    dd.fp##name = reinterpret_cast<PFN_vk##name>(dd.gdpa(dd.device, "vk" #name))

static void InitDeviceFunctions(DeviceData& dd)
{
    LOAD_DEV_FN(dd, CreateCommandPool);
    LOAD_DEV_FN(dd, AllocateCommandBuffers);
    LOAD_DEV_FN(dd, BeginCommandBuffer);
    LOAD_DEV_FN(dd, EndCommandBuffer);
    LOAD_DEV_FN(dd, CmdBlitImage);
    LOAD_DEV_FN(dd, QueueSubmit);
    LOAD_DEV_FN(dd, DestroyCommandPool);
}

// ============================================================
// Hook：vkCreateDevice
// ============================================================
VK_LAYER_EXPORT VkResult VKAPI_CALL
ReShade_vkCreateDevice(
    VkPhysicalDevice             physDevice,
    const VkDeviceCreateInfo*    pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice*                    pDevice)
{
    // 获取链下一层的 vkCreateDevice
    auto layerCI = const_cast<VkDeviceCreateInfo*>(pCreateInfo);
    VkLayerDeviceCreateInfo* layerInfo =
        reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(pCreateInfo->pNext));

    while (layerInfo &&
           !(layerInfo->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO &&
             layerInfo->function == VK_LAYER_LINK_INFO)) {
        layerInfo = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(layerInfo->pNext));
    }

    if (!layerInfo) {
        LOGE("vkCreateDevice: no layer link info found");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PFN_vkGetInstanceProcAddr gipa = layerInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr   gdpa = layerInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    layerInfo->u.pLayerInfo         = layerInfo->u.pLayerInfo->pNext;

    auto fpCreateDevice = reinterpret_cast<PFN_vkCreateDevice>(
        gipa(VK_NULL_HANDLE, "vkCreateDevice"));

    VkResult result = fpCreateDevice(physDevice, layerCI, pAllocator, pDevice);
    if (result != VK_SUCCESS) return result;

    // 记录设备信息
    DeviceData dd;
    dd.device     = *pDevice;
    dd.physDevice = physDevice;
    dd.gdpa       = gdpa;

    InitDeviceFunctions(dd);
    dd.fpQueuePresent = reinterpret_cast<PFN_vkQueuePresentKHR>(
        gdpa(*pDevice, "vkQueuePresentKHR"));

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_devices[GetKey(*pDevice)] = dd;
    }

    LOGI("vkCreateDevice hooked: device=%p", (void*)*pDevice);
    return VK_SUCCESS;
}

// ============================================================
// Hook：vkDestroyDevice
// ============================================================
VK_LAYER_EXPORT void VKAPI_CALL
ReShade_vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator)
{
    void* key = GetKey(device);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_devices.find(key);
        if (it != g_devices.end()) {
            DeviceData& dd = it->second;
            if (dd.cmdPool != VK_NULL_HANDLE && dd.fpDestroyCommandPool) {
                dd.fpDestroyCommandPool(device, dd.cmdPool, pAllocator);
            }
            g_devices.erase(it);
        }
    }

    // 调用下一层
    // （简化：在真实场景中需要调用链下一层）
    LOGI("vkDestroyDevice: device=%p", (void*)device);
}

// ============================================================
// Hook：vkQueuePresentKHR（核心拦截点）
// ============================================================
VK_LAYER_EXPORT VkResult VKAPI_CALL
ReShade_vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{
    void* key = GetKey(queue);
    DeviceData* dd = nullptr;

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_devices.find(key);
        if (it != g_devices.end()) dd = &it->second;
    }

    if (dd && dd->fpQueuePresent) {
        // --------------------------------------------------------
        // 后处理注入点
        // 实际项目中：
        //   1. 获取当前 swapchain image（从 pPresentInfo->pImageIndices）
        //   2. 记录后处理 command buffer（blit → offscreen → apply shader → blit back）
        //   3. 在 vkQueueSubmit 中插入等待/信号量链
        //   4. 最后调用原始 vkQueuePresentKHR
        // --------------------------------------------------------
        LOGD("vkQueuePresentKHR: queue=%p swapchains=%u", (void*)queue, pPresentInfo->swapchainCount);

        // TODO: 调用 ShaderRuntime 执行后处理
        // PostProcessVulkan(dd, queue, pPresentInfo);

        return dd->fpQueuePresent(queue, pPresentInfo);
    }

    // Fallback
    return VK_ERROR_DEVICE_LOST;
}

// ============================================================
// vkCreateInstance
// ============================================================
VK_LAYER_EXPORT VkResult VKAPI_CALL
ReShade_vkCreateInstance(
    const VkInstanceCreateInfo*  pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance*                  pInstance)
{
    VkLayerInstanceCreateInfo* layerInfo =
        reinterpret_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(pCreateInfo->pNext));

    while (layerInfo &&
           !(layerInfo->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO &&
             layerInfo->function == VK_LAYER_LINK_INFO)) {
        layerInfo = reinterpret_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(layerInfo->pNext));
    }

    if (!layerInfo) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr gipa = layerInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    layerInfo->u.pLayerInfo         = layerInfo->u.pLayerInfo->pNext;

    auto fpCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(
        gipa(VK_NULL_HANDLE, "vkCreateInstance"));

    VkResult result = fpCreateInstance(pCreateInfo, pAllocator, pInstance);
    if (result != VK_SUCCESS) return result;

    InstanceData id;
    id.instance = *pInstance;
    id.gipa     = gipa;

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_instances[GetKey(*pInstance)] = id;
    }

    LOGI("vkCreateInstance hooked: instance=%p", (void*)*pInstance);
    return VK_SUCCESS;
}

// ============================================================
// Layer 入口：vkGetDeviceProcAddr
// ============================================================
VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL
ReShade_vkGetDeviceProcAddr(VkDevice device, const char* pName)
{
    if (strcmp(pName, "vkGetDeviceProcAddr")  == 0) return (PFN_vkVoidFunction)ReShade_vkGetDeviceProcAddr;
    if (strcmp(pName, "vkCreateDevice")       == 0) return (PFN_vkVoidFunction)ReShade_vkCreateDevice;
    if (strcmp(pName, "vkDestroyDevice")      == 0) return (PFN_vkVoidFunction)ReShade_vkDestroyDevice;
    if (strcmp(pName, "vkQueuePresentKHR")    == 0) return (PFN_vkVoidFunction)ReShade_vkQueuePresentKHR;

    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_devices.find(GetKey(device));
    if (it != g_devices.end() && it->second.gdpa) {
        return it->second.gdpa(device, pName);
    }
    return nullptr;
}

// ============================================================
// Layer 入口：vkGetInstanceProcAddr
// ============================================================
VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL
ReShade_vkGetInstanceProcAddr(VkInstance instance, const char* pName)
{
    if (strcmp(pName, "vkGetInstanceProcAddr")  == 0) return (PFN_vkVoidFunction)ReShade_vkGetInstanceProcAddr;
    if (strcmp(pName, "vkGetDeviceProcAddr")    == 0) return (PFN_vkVoidFunction)ReShade_vkGetDeviceProcAddr;
    if (strcmp(pName, "vkCreateInstance")       == 0) return (PFN_vkVoidFunction)ReShade_vkCreateInstance;
    if (strcmp(pName, "vkCreateDevice")         == 0) return (PFN_vkVoidFunction)ReShade_vkCreateDevice;
    if (strcmp(pName, "vkDestroyDevice")        == 0) return (PFN_vkVoidFunction)ReShade_vkDestroyDevice;
    if (strcmp(pName, "vkQueuePresentKHR")      == 0) return (PFN_vkVoidFunction)ReShade_vkQueuePresentKHR;

    // 枚举 Layer 属性
    if (strcmp(pName, "vkEnumerateInstanceLayerProperties") == 0 ||
        strcmp(pName, "vkEnumerateDeviceLayerProperties")   == 0) {
        return (PFN_vkVoidFunction)[](VkPhysicalDevice pd,
                                      uint32_t*        pCount,
                                      VkLayerProperties* pProps) -> VkResult {
            (void)pd;
            if (!pCount) return VK_ERROR_INITIALIZATION_FAILED;
            if (!pProps) { *pCount = 1; return VK_SUCCESS; }
            if (*pCount < 1) return VK_INCOMPLETE;
            *pCount = 1;
            strncpy(pProps[0].layerName,    LAYER_NAME, VK_MAX_EXTENSION_NAME_SIZE);
            strncpy(pProps[0].description,  LAYER_DESC, VK_MAX_DESCRIPTION_SIZE);
            pProps[0].specVersion           = LAYER_SPEC;
            pProps[0].implementationVersion = LAYER_IMPL;
            return VK_SUCCESS;
        };
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_instances.find(GetKey(instance));
    if (it != g_instances.end() && it->second.gipa) {
        return it->second.gipa(instance, pName);
    }
    return nullptr;
}
