#pragma once
/**
 * Module 1: ShaderLoader
 * ──────────────────────
 * 职责：从磁盘读取 .fx / .hlsl 文件，解析出完整的 Shader 描述结构。
 *
 * 对外接口完全纯数据，不依赖任何 GL/Vk 接口。
 * 其他模块只需 #include "shader_loader.h"。
 */

#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <optional>

namespace reshade {

// ════════════════════════════════════════════════════════
// 数据类型
// ════════════════════════════════════════════════════════

/// Uniform 变量的值类型
using UniformValue = std::variant<float, int, bool,
    std::array<float,2>, std::array<float,3>, std::array<float,4>>;

/// UI 注解（来自 <ui_type="slider"> 等）
struct UiAnnotation {
    std::string type;           // "slider" | "color" | "checkbox" | "combo"
    std::string label;          // 显示名称
    float       min  = 0.f;
    float       max  = 1.f;
    float       step = 0.01f;
};

/// Uniform 变量描述
struct UniformDesc {
    std::string   name;
    std::string   type;         // "float" | "float2" | "float4" | "int" | "bool" ...
    UniformValue  defaultVal;   // 默认值
    UiAnnotation  ui;           // UI 提示
    std::string   semantic;     // 特殊语义："TIMER" | "FRAMECOUNT" | "" (普通)
};

/// 纹理描述
struct TextureDesc {
    std::string name;
    std::string source;         // "" = backbuffer, 否则为文件路径
    int         width  = 0;     // 0 = use backbuffer size
    int         height = 0;
    std::string format = "RGBA8";
};

/// 采样器描述
struct SamplerDesc {
    std::string name;
    std::string textureName;    // 对应的 TextureDesc::name
    std::string filter   = "LINEAR";
    std::string addressU = "CLAMP";
    std::string addressV = "CLAMP";
};

/// 单个 Pass 描述
struct PassDesc {
    std::string name;
    std::string vertexEntry;    // 顶点着色器入口函数名
    std::string pixelEntry;     // 像素着色器入口函数名
    std::string renderTarget;   // "" = backbuffer
    bool        clearTarget = false;
    std::string blendOp    = "ADD";
    std::string srcBlend   = "ONE";
    std::string dstBlend   = "ZERO";
};

/// Technique（一组 Pass）
struct TechniqueDesc {
    std::string           name;
    bool                  enabled = true;
    std::vector<PassDesc> passes;
};

/// 完整的 Shader 文件描述（解析结果）
struct ShaderFile {
    std::string path;                       // 原始文件路径
    std::string rawSource;                  // 原始 HLSL/FX 源码（去注释后）
    std::string preprocessedSource;        // 宏展开后源码（可选）

    std::vector<UniformDesc>   uniforms;
    std::vector<TextureDesc>   textures;
    std::vector<SamplerDesc>   samplers;
    std::vector<TechniqueDesc> techniques;

    // 错误信息（若解析失败）
    std::string errorLog;
    bool        valid = false;
};

// ════════════════════════════════════════════════════════
// ShaderLoader 接口
// ════════════════════════════════════════════════════════

class ShaderLoader {
public:
    ShaderLoader() = default;

    /**
     * 加载并解析 .fx 或 .hlsl 文件
     * @param path  绝对路径
     * @return 解析结果；result.valid=false 时 errorLog 有详情
     */
    ShaderFile load(const std::string& path);

    /**
     * 从内存字符串解析（用于单元测试或内置 shader）
     * @param source  HLSL/FX 源码字符串
     * @param name    虚拟文件名（用于错误提示）
     */
    ShaderFile loadFromSource(const std::string& source,
                              const std::string& name = "<memory>");

    /**
     * 扫描目录，返回所有 .fx / .hlsl 文件路径
     */
    static std::vector<std::string> scanDirectory(const std::string& dir);

private:
    // ── 内部解析步骤 ─────────────────────────────────────
    std::string  readFile(const std::string& path);
    std::string  stripComments(const std::string& src);
    std::string  preprocessIncludes(const std::string& src,
                                    const std::string& baseDir);

    void parseUniforms (const std::string& src, ShaderFile& out);
    void parseTextures (const std::string& src, ShaderFile& out);
    void parseSamplers (const std::string& src, ShaderFile& out);
    void parseTechniques(const std::string& src, ShaderFile& out);

    // ── 注解解析 ─────────────────────────────────────────
    static UiAnnotation parseAnnotationBlock(const std::string& block);

    // ── 默认值解析 ────────────────────────────────────────
    static UniformValue parseDefaultValue(const std::string& type,
                                          const std::string& literal);
};

} // namespace reshade
