#pragma once
/**
 * Module 5: ShaderRuntime
 * ────────────────────────
 * 职责：管理完整的后处理 Pipeline。
 *
 * 功能：
 *   - 加载 ShaderFile → 编译 GLSL → 链接 GL Program
 *   - 管理多个 Effect（每个 Effect 含多个 Pass）
 *   - 按顺序执行 Effect 链：BackBuffer → FBO → FBO → ... → BackBuffer
 *   - 管理 uniform 参数（支持实时更新）
 *   - 每 Pass 有独立的 RenderTarget / BlendState
 *
 * 依赖：ShaderLoader, HlslConverter, LutLoader, GpuHook（回调方）
 */
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <GLES3/gl3.h>
#include "../../../modules/shader_loader/include/shader_loader.h"

namespace reshade {

// ════════════════════════════════════════════════════════
// 编译后的单个 Pass
// ════════════════════════════════════════════════════════

struct CompiledPass {
    std::string name;
    GLuint      program    = 0;     // 链接后的 GL Program
    GLuint      renderTarget= 0;    // 0 = 写回 backbuffer FBO
    bool        enabled    = true;

    // Uniform 位置缓存（避免每帧 glGetUniformLocation）
    std::unordered_map<std::string, GLint> uniformLocs;

    // 当前 uniform 值
    std::unordered_map<std::string, float> floatUniforms;

    void setFloat (const std::string& name, float v);
    void setFloat2(const std::string& name, float x, float y);
    void setFloat3(const std::string& name, float x, float y, float z);
    void setFloat4(const std::string& name, float x, float y, float z, float w);
    void setInt   (const std::string& name, int v);
    void setTex   (const std::string& name, GLuint texId, int unit);

    void applyUniforms() const;
};

// ════════════════════════════════════════════════════════
// 编译后的 Effect（对应一个 .fx 文件）
// ════════════════════════════════════════════════════════

struct CompiledEffect {
    std::string              name;        // 来自文件名或 technique 名
    std::string              sourcePath;  // 原始 .fx 路径
    bool                     enabled = true;
    int                      order   = 0; // 执行顺序（越小越先执行）

    std::vector<CompiledPass> passes;

    // 该 Effect 的全局参数（uniform）
    std::vector<UniformDesc>  uniforms;

    // Effect 级别的纹理（sampler2D）
    std::unordered_map<std::string, GLuint> textures;  // name → GL texId
};

// ════════════════════════════════════════════════════════
// ShaderRuntime 接口
// ════════════════════════════════════════════════════════

class ShaderRuntime {
public:
    explicit ShaderRuntime();
    ~ShaderRuntime();

    // ── Effect 管理 ──────────────────────────────────────

    /**
     * 从 ShaderFile 编译并加入 pipeline
     * @return effect id（用于后续操作）
     */
    int  addEffect(const ShaderFile& sf);

    /** 从文件路径加载（内部调用 ShaderLoader + HlslConverter） */
    int  loadEffect(const std::string& path);

    void removeEffect(int effectId);
    void setEffectEnabled(int effectId, bool on);
    void setEffectOrder(int effectId, int order);  // 调整执行顺序

    // ── 参数更新 ─────────────────────────────────────────

    void setUniform(int effectId, const std::string& name, float value);
    void setUniform(int effectId, const std::string& name,
                    float x, float y, float z, float w);

    /** 批量更新参数（Config 系统用） */
    void applyParams(int effectId,
                     const std::unordered_map<std::string,float>& params);

    // ── LUT 绑定 ─────────────────────────────────────────

    void bindLUT(int effectId, GLuint lutTex, float strength = 1.f);

    // ── Pipeline 执行 ─────────────────────────────────────

    /**
     * 执行完整的后处理链（由 GpuHook 的回调触发）
     * @param backbufferFBO  原始帧的 FBO id（通常为 0）
     * @param width          surface 宽度
     * @param height         surface 高度
     */
    void render(GLint backbufferFBO, int width, int height);

    // ── 尺寸通知 ─────────────────────────────────────────

    void onResize(int width, int height);

    // ── 查询 ─────────────────────────────────────────────

    std::vector<int>            effectIds()   const;
    const CompiledEffect*       getEffect(int id) const;
    std::vector<UniformDesc>    uniformsOf(int effectId) const;

private:
    // ── FBO 池（双缓冲）─────────────────────────────────
    struct FboPool {
        GLuint fbo[2]  = {0,0};
        GLuint tex[2]  = {0,0};
        int    width   = 0;
        int    height  = 0;
        int    current = 0;

        void init(int w, int h);
        void resize(int w, int h);
        void destroy();
        GLuint srcTex()  const { return tex[current]; }
        GLuint dstFBO()  const { return fbo[1 - current]; }
        void   swap()          { current = 1 - current; }
    };

    // ── 全屏三角 VAO ─────────────────────────────────────
    GLuint vao_ = 0;

    void ensureVAO();

    // ── Effect 存储 ──────────────────────────────────────
    std::unordered_map<int, CompiledEffect> effects_;
    int nextId_ = 1;

    // ── FBO 管理 ─────────────────────────────────────────
    FboPool fboPool_;

    // ── 内部编译 ─────────────────────────────────────────
    GLuint compileShader(GLenum type, const std::string& src);
    GLuint linkProgram  (const std::string& vs, const std::string& fs);

    // ── 执行单个 Pass ─────────────────────────────────────
    void  executePass(CompiledPass& pass,
                      GLuint srcTex,
                      GLuint dstFBO,
                      int w, int h);

    // ── 内置 Uniform 注入（TIMER / FRAMECOUNT / TexelSize）
    void  injectBuiltinUniforms(CompiledPass& pass, int w, int h);

    uint64_t frameCount_ = 0;
};

} // namespace reshade
