#pragma once
/**
 * Module 2: HlslConverter
 * ────────────────────────
 * 职责：将 HLSL / FX 源码转换为 GLSL ES 3.0。
 *
 * 转换策略分三层：
 *   Layer A — 词法替换（类型名、内置函数）
 *   Layer B — 语义处理（语义注解、入出参数）
 *   Layer C — 结构重写（cbuffer→uniform、sampler 合并、mul 方向）
 */
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace reshade {

// ════════════════════════════════════════════════════════
// 转换结果
// ════════════════════════════════════════════════════════

enum class ShaderStage { Vertex, Fragment };

struct ConvertResult {
    std::string glsl;       // 生成的 GLSL ES 3.0 源码
    std::string errorLog;   // 转换时的警告/错误
    ShaderStage stage;
    bool        success = false;
};

// ════════════════════════════════════════════════════════
// 转换选项
// ════════════════════════════════════════════════════════

struct ConvertOptions {
    bool    emitPrecision   = true;     // 生成 precision highp float;
    bool    flipY           = false;    // vertex: gl_Position.y = -y
    bool    fixMulOrder     = true;     // HLSL mul(A,B) → GLSL B*A
    bool    wrapWithMain    = true;     // 把入口函数重命名为 main()
    int     glslVersion     = 300;      // 300 = GLSL ES 3.0
    std::string customPreamble;         // 用户自定义 #define 等
};

// ════════════════════════════════════════════════════════
// HlslConverter 接口
// ════════════════════════════════════════════════════════

class HlslConverter {
public:
    explicit HlslConverter(ConvertOptions opts = {});

    /**
     * 将 HLSL 源码中的某个入口函数转换为完整 GLSL shader
     *
     * @param hlslSrc     完整 HLSL/FX 源码（已去注释、已展开 #include）
     * @param entryPoint  入口函数名（如 "VS_Main" / "PS_Main"）
     * @param stage       ShaderStage::Vertex 或 Fragment
     */
    ConvertResult convert(const std::string& hlslSrc,
                          const std::string& entryPoint,
                          ShaderStage stage);

    /// 注册自定义替换规则（正则 → 替换字符串）
    void addCustomRule(const std::string& pattern, const std::string& replacement);

    /// 获取当前支持的完整替换表（调试用）
    const std::unordered_map<std::string, std::string>& typeMap()     const;
    const std::vector<std::pair<std::string,std::string>>& funcMap() const;

private:
    ConvertOptions opts_;

    // ── Layer A: 词法替换 ───────────────────────────────
    std::string layerA_types    (const std::string& src) const;
    std::string layerA_functions(const std::string& src) const;
    std::string layerA_custom   (const std::string& src) const;

    // ── Layer B: 语义处理 ───────────────────────────────
    std::string layerB_semantics(const std::string& src,
                                  ShaderStage stage,
                                  const std::string& entry,
                                  std::string& outIoDecls) const;

    // ── Layer C: 结构重写 ───────────────────────────────
    std::string layerC_cbuffer  (const std::string& src) const;
    std::string layerC_samplers (const std::string& src) const;
    std::string layerC_mul      (const std::string& src) const;
    std::string layerC_fragOut  (const std::string& src) const;
    std::string layerC_entryWrap(const std::string& src,
                                  const std::string& entry,
                                  ShaderStage stage) const;

    // ── 自定义规则 ────────────────────────────────────
    std::vector<std::pair<std::string,std::string>> customRules_;

    // ── 内置表 ─────────────────────────────────────────
    static const std::unordered_map<std::string,std::string> BUILTIN_TYPES;
    static const std::vector<std::pair<std::string,std::string>> BUILTIN_FUNCS;
};

} // namespace reshade
