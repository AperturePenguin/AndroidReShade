/**
 * bytehook_stub.h
 * ByteDance bhook (bytehook) 轻量存根头文件
 *
 * 真实 bhook 从 https://github.com/bytedance/bhook 获取
 * 本文件提供 API 签名，供编译期检查和模拟 hook。
 *
 * 接入方式：
 *   1. 从 bhook releases 下载 libbytehook.so + bytehook.h
 *   2. 放入 app/src/main/cpp/third_party/bhook/
 *   3. 将 #define BHOOK_STUB 注释掉即可切换到真实库
 */

#pragma once
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// 类型定义
// ============================================================
typedef void* bytehook_stub_t;

typedef void (*bytehook_hooked_t)(
    bytehook_stub_t stub,
    int             result,
    const char*     callee_path_name,
    const char*     sym_name,
    void*           new_func,
    void*           prev_func,
    void*           hooked_arg
);

#define BYTEHOOK_STATUS_CODE_OK              0
#define BYTEHOOK_STATUS_CODE_INVALID_ARG    -1
#define BYTEHOOK_STATUS_CODE_INITERR        -2

// hook 类型
typedef enum {
    BYTEHOOK_TYPE_UNKNOWN  = 0,
    BYTEHOOK_TYPE_ELF      = 1,   // PLT hook
    BYTEHOOK_TYPE_PROXY    = 2    // proxy hook
} bytehook_type_t;

// ============================================================
// 初始化
// ============================================================
static inline int bytehook_init(int type, bool debug)
{
    (void)type; (void)debug;
    return BYTEHOOK_STATUS_CODE_OK;  // stub: always success
}

// ============================================================
// hook 单个调用方
// ============================================================
static inline bytehook_stub_t bytehook_hook_single(
    const char*       callee_path_name,
    const char*       callee_address,
    const char*       sym_name,
    void*             new_func,
    bytehook_hooked_t hooked,
    void*             hooked_arg)
{
    (void)callee_path_name; (void)callee_address;
    (void)sym_name; (void)new_func;
    (void)hooked; (void)hooked_arg;
    return (bytehook_stub_t)0x1; // stub: return non-null
}

// ============================================================
// hook 所有调用方
// ============================================================
static inline bytehook_stub_t bytehook_hook_all(
    const char*       callee_path_name,
    const char*       sym_name,
    void*             new_func,
    bytehook_hooked_t hooked,
    void*             hooked_arg)
{
    (void)callee_path_name; (void)sym_name; (void)new_func;
    (void)hooked; (void)hooked_arg;
    return (bytehook_stub_t)0x1;
}

// ============================================================
// unhook
// ============================================================
static inline int bytehook_unhook(bytehook_stub_t stub)
{
    (void)stub;
    return BYTEHOOK_STATUS_CODE_OK;
}

// ============================================================
// 调用原始函数（在 hook 函数内部使用）
// ============================================================
#define BYTEHOOK_CALL_PREV(func, ...)  func(__VA_ARGS__)
#define BYTEHOOK_POP_STACK()           do {} while(0)
#define BYTEHOOK_RETURN_ADDRESS()      __builtin_return_address(0)

#ifdef __cplusplus
}
#endif
