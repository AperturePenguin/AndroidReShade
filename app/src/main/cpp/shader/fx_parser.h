#pragma once
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────
// 数据结构
// ─────────────────────────────────────────────────────────
struct UniformInfo {
    std::string name;
    std::string type;
    std::string uiType;    // "slider" / "color" / "checkbox" 等
    std::string uiLabel;
    float       defaultValue{0.0f};
    float       uiMin{0.0f};
    float       uiMax{1.0f};
    float       uiStep{0.01f};
    float       currentValue{0.0f};  // 运行时值
};

struct PassInfo {
    std::string name;
    std::string vertexEntry;
    std::string pixelEntry;
};

struct TechniqueInfo {
    std::string           name;
    std::vector<PassInfo> passes;
    bool                  enabled{true};
};

struct TextureInfo {
    std::string name;
    std::string source;  // 外部文件路径
    int         width{0}, height{0};
};

struct FxEffect {
    std::string                 filePath;
    std::string                 sourceCode;   // 原始 HLSL 源码
    std::string                 glslCode;     // 转译后的 GLSL 代码
    std::vector<UniformInfo>    uniforms;
    std::vector<TechniqueInfo>  techniques;
    std::vector<TextureInfo>    textures;
    bool                        compiled{false};
};

// ─────────────────────────────────────────────────────────
// FX 解析器
// ─────────────────────────────────────────────────────────
class FxParser {
public:
    FxEffect parse(const std::string& filePath);

private:
    std::string removeComments(const std::string& src);
    void        parseUniforms(const std::string& src, FxEffect& effect);
    void        parseTechniques(const std::string& src, FxEffect& effect);
    void        parseTextures(const std::string& src, FxEffect& effect);
};
