#pragma once
#include <string>

enum class ShaderStage { Vertex, Fragment };

class HlslTranspiler {
public:
    struct Result {
        std::string glsl;
        std::string errorLog;
        ShaderStage stage{ShaderStage::Fragment};
        bool        success{false};
    };

    Result transpile(const std::string& hlslSrc,
                     ShaderStage stage,
                     const std::string& entryPoint = "main");

private:
    std::string removeHlslPragmas(const std::string& src);
    std::string replaceTypes(const std::string& src);
    std::string replaceFunctions(const std::string& src);
    std::string transformCBuffer(const std::string& src);
    std::string transformSamplers(const std::string& src);
    std::string transformSemantics(const std::string& src, ShaderStage stage,
                                   const std::string& entry, std::string& ioDecls);
    std::string transformMul(const std::string& src);
    std::string transformFragOutput(const std::string& src);
};
