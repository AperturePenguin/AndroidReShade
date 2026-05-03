/**
 * vk_hook.cpp — Vulkan Layer 实现
 *
 * 作为一个隐式 Vulkan Layer 拦截 vkQueuePresentKHR，
 * 在 Present 前执行后处理。
 *
 * 部署方式：
 *   将 libVkLayer_reshade.so 放入 /data/local/debug/vulkan/
 *   或通过 app 的 VK_INSTANCE_LAYERS 环境变量加载
 */

#include "vk_hook.h"
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <android/log.h>
#include <unordered_map>
#include <mutex>
#include <vector>

#define LOG_TAG "ReShadeVK"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ─────────────────────────────────────────────────────────
// 分发表存储
// ─────────────────────────────────────────────────────────
struct InstanceData {
    VkInstance                      instance;
    PFN_vkGetInstanceProcAddr       getInstanceProcAddr;
    PFN_vkDestroyInstance           destroyInstance;
};

struct DeviceData {
    VkDevice                        device;
    VkPhysicalDevice                physDevice;
    VkQueue                         gfxQueue;
    uint32_t                        gfxQueueFamily;
    PFN_vkGetDeviceProcAddr         getDeviceProcAddr;
    PFN_vkDestroyDevice             destroyDevice;
    PFN_vkQueuePresentKHR           queuePresent;
    PFN_vkAllocateCommandBuffers    allocCmdBufs;
    PFN_vkBeginCommandBuffer        beginCmdBuf;
    PFN_vkEndCommandBuffer          endCmdBuf;
    PFN_vkQueueSubmit               queueSubmit;
    PFN_vkCreateImageView           createImageView;
    PFN_vkCreateRenderPass          createRenderPass;
    PFN_vkCreateFramebuffer         createFramebuffer;
    PFN_vkCreateGraphicsPipelines   createGraphicsPipelines;
    PFN_vkCmdBeginRenderPass        cmdBeginRenderPass;
    PFN_vkCmdEndRenderPass          cmdEndRenderPass;
    PFN_vkCmdBindPipeline           cmdBindPipeline;
    PFN_vkCmdDraw                   cmdDraw;
};

static std::mutex                              g_instanceMutex;
static std::unordered_map<VkInstance, InstanceData> g_instances;
static std::mutex                              g_deviceMutex;
static std::unordered_map<VkDevice, DeviceData>     g_devices;

// ─────────────────────────────────────────────────────────
// 后处理 Vulkan 渲染器（简化版）
// ─────────────────────────────────────────────────────────
struct VkPostProcess {
    VkDevice          device;
    VkRenderPass      renderPass;
    VkPipeline        pipeline;
    VkPipelineLayout  pipelineLayout;
    VkDescriptorPool  descPool;
    VkDescriptorSet   descSet;
    VkSampler         sampler;
    bool              initialized = false;

    // 效果参数
    float brightness  = 0.0f;
    float contrast    = 1.0f;
    float saturation  = 1.0f;
    float sharpness   = 0.0f;

    void init(VkDevice dev, VkPhysicalDevice physDev,
              VkFormat swapFormat, uint32_t width, uint32_t height);
    void render(VkCommandBuffer cmd, VkImage srcImage, VkImageView srcView,
                VkFramebuffer fb, uint32_t w, uint32_t h);
    void destroy();
};

static VkPostProcess g_postProcess;

// 后处理 SPIR-V（全屏三角 + 亮度/对比度/饱和度/锐化）
// 实际项目中由 shader 编译器动态生成
static const uint32_t VERT_SPV[] = {
    // 内置的全屏三角顶点 shader SPIR-V 二进制（此处为占位）
    // 实际使用时用 glslangValidator 编译并嵌入
    0x07230203, 0x00010000, 0x000d0007, /* ... 完整 SPV ... */
};

static const uint32_t FRAG_SPV[] = {
    // 后处理 fragment shader SPIR-V 二进制
    0x07230203, 0x00010000, 0x000d0007, /* ... 完整 SPV ... */
};

// ─────────────────────────────────────────────────────────
// Layer 入口：vkGetInstanceProcAddr
// ─────────────────────────────────────────────────────────
static VKAPI_ATTR VkResult VKAPI_CALL reshade_vkCreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance)
{
    // 从链表获取下一层
    auto* layerInfo = reinterpret_cast<VkLayerInstanceCreateInfo*>(
        const_cast<void*>(pCreateInfo->pNext));
    while (layerInfo &&
           !(layerInfo->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO &&
             layerInfo->function == VK_LAYER_LINK_INFO)) {
        layerInfo = reinterpret_cast<VkLayerInstanceCreateInfo*>(
            const_cast<void*>(layerInfo->pNext));
    }
    if (!layerInfo) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr nextGIPA = layerInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    layerInfo->u.pLayerInfo = layerInfo->u.pLayerInfo->pNext;

    auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(
        nextGIPA(VK_NULL_HANDLE, "vkCreateInstance"));
    VkResult result = createInstance(pCreateInfo, pAllocator, pInstance);
    if (result != VK_SUCCESS) return result;

    std::lock_guard<std::mutex> lock(g_instanceMutex);
    g_instances[*pInstance] = {
        *pInstance,
        nextGIPA,
        reinterpret_cast<PFN_vkDestroyInstance>(nextGIPA(*pInstance, "vkDestroyInstance"))
    };

    LOGI("vkCreateInstance hooked: %p", *pInstance);
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL reshade_vkCreateDevice(
    VkPhysicalDevice physDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice)
{
    auto* layerInfo = reinterpret_cast<VkLayerDeviceCreateInfo*>(
        const_cast<void*>(pCreateInfo->pNext));
    while (layerInfo &&
           !(layerInfo->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO &&
             layerInfo->function == VK_LAYER_LINK_INFO)) {
        layerInfo = reinterpret_cast<VkLayerDeviceCreateInfo*>(
            const_cast<void*>(layerInfo->pNext));
    }
    if (!layerInfo) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr nextGIPA = layerInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr   nextGDPA = layerInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    layerInfo->u.pLayerInfo = layerInfo->u.pLayerInfo->pNext;

    auto createDevice = reinterpret_cast<PFN_vkCreateDevice>(
        nextGIPA(VK_NULL_HANDLE, "vkCreateDevice"));
    VkResult result = createDevice(physDevice, pCreateInfo, pAllocator, pDevice);
    if (result != VK_SUCCESS) return result;

    // 填充 DeviceData
    DeviceData dd{};
    dd.device       = *pDevice;
    dd.physDevice   = physDevice;
    dd.getDeviceProcAddr = nextGDPA;
#define LOAD_DEV(fn) dd.fn = reinterpret_cast<PFN_vk##fn>(nextGDPA(*pDevice, "vk" #fn))
    LOAD_DEV(DestroyDevice);
    LOAD_DEV(QueuePresentKHR);
    LOAD_DEV(AllocateCommandBuffers);
    LOAD_DEV(BeginCommandBuffer);
    LOAD_DEV(EndCommandBuffer);
    LOAD_DEV(QueueSubmit);
    LOAD_DEV(CreateImageView);
    LOAD_DEV(CreateRenderPass);
    LOAD_DEV(CreateFramebuffer);
    LOAD_DEV(CreateGraphicsPipelines);
    LOAD_DEV(CmdBeginRenderPass);
    LOAD_DEV(CmdEndRenderPass);
    LOAD_DEV(CmdBindPipeline);
    LOAD_DEV(CmdDraw);
#undef LOAD_DEV

    std::lock_guard<std::mutex> lock(g_deviceMutex);
    g_devices[*pDevice] = dd;

    LOGI("vkCreateDevice hooked: %p", *pDevice);
    return VK_SUCCESS;
}

// ─────────────────────────────────────────────────────────
// 核心拦截：vkQueuePresentKHR
// ─────────────────────────────────────────────────────────
static VKAPI_ATTR VkResult VKAPI_CALL reshade_vkQueuePresentKHR(
    VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{
    std::lock_guard<std::mutex> lock(g_deviceMutex);

    // 找到对应的 device
    DeviceData* dd = nullptr;
    for (auto& [dev, data] : g_devices) {
        if (data.gfxQueue == queue) { dd = &data; break; }
    }

    if (dd) {
        // TODO: 在此执行后处理 Pass
        // 1. 获取当前 swapchain image
        // 2. 提交后处理 command buffer
        // 3. 等待完成后再 present
        // (完整实现需要在 vkCreateSwapchainKHR 时建立 per-image 资源)
        LOGI("vkQueuePresentKHR intercepted, executing post-process...");
    }

    // 调用原始 present
    if (dd) return dd->queuePresent(queue, pPresentInfo);
    return VK_ERROR_DEVICE_LOST;
}

// ─────────────────────────────────────────────────────────
// Layer 导出符号（Android Vulkan loader 要求）
// ─────────────────────────────────────────────────────────
extern "C" {

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
    VkInstance instance, const char* pName)
{
#define INTERCEPT(fn) if (strcmp(pName, #fn) == 0) return (PFN_vkVoidFunction)reshade_##fn;
    INTERCEPT(vkCreateInstance)
    INTERCEPT(vkCreateDevice)
#undef INTERCEPT

    std::lock_guard<std::mutex> lock(g_instanceMutex);
    auto it = g_instances.find(instance);
    if (it != g_instances.end())
        return it->second.getInstanceProcAddr(instance, pName);
    return nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(
    VkDevice device, const char* pName)
{
    if (strcmp(pName, "vkQueuePresentKHR") == 0)
        return (PFN_vkVoidFunction)reshade_vkQueuePresentKHR;

    std::lock_guard<std::mutex> lock(g_deviceMutex);
    auto it = g_devices.find(device);
    if (it != g_devices.end())
        return it->second.getDeviceProcAddr(device, pName);
    return nullptr;
}

// Layer 枚举信息
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(
    uint32_t* count, VkLayerProperties* props)
{
    if (!props) { *count = 1; return VK_SUCCESS; }
    strcpy(props[0].layerName,             "VK_LAYER_RESHADE_android");
    strcpy(props[0].description,           "AndroidReShade post-processing layer");
    props[0].specVersion        = VK_API_VERSION_1_1;
    props[0].implementationVersion = 1;
    *count = 1;
    return VK_SUCCESS;
}

} // extern "C"
