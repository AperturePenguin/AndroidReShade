#pragma once
/**
 * Module 3: LutLoader
 * ────────────────────
 * 职责：把 .png / .cube LUT 文件加载并上传为 GL_TEXTURE_3D。
 *
 * 支持格式：
 *   .cube — Adobe/DaVinci CUBE 格式（LUT_3D_SIZE N, 3列 float 数据）
 *   .png  — Hald CLUT（512×512, level-8）或条带格式（N²×N）
 *
 * 输出：GLuint（GL_TEXTURE_3D, RGBA8, 32³ 标准尺寸）
 */
#include <string>
#include <vector>
#include <GLES3/gl3.h>

namespace reshade {

// ════════════════════════════════════════════════════════
// LUT 原始数据（平台无关）
// ════════════════════════════════════════════════════════

struct LutData {
    int                  size = 0;         // 立方体边长（通常 32 或 64）
    std::vector<float>   rgb;              // size^3 × 3 floats，顺序 [B][G][R]
    bool                 valid = false;
    std::string          errorLog;
};

// ════════════════════════════════════════════════════════
// LutLoader 接口
// ════════════════════════════════════════════════════════

class LutLoader {
public:
    static constexpr int TARGET_SIZE = 32;  // 统一重采样目标尺寸

    /**
     * 加载文件，返回 GPU 纹理 id（GL_TEXTURE_3D）
     * 返回 0 表示失败。
     */
    GLuint load(const std::string& path);

    /**
     * 仅解析为 LutData，不上传 GPU（用于调试/预览）
     */
    LutData parse(const std::string& path);

    /**
     * 将 LutData 上传为 GL_TEXTURE_3D
     * 内部会将 LutData 重采样到 TARGET_SIZE
     */
    static GLuint uploadToGPU(const LutData& data);

    // ── 格式解析器（可独立使用）─────────────────────────
    static LutData parseCube(const std::string& path);
    static LutData parsePng (const std::string& path);

private:
    /**
     * 三线性插值，将任意尺寸 LUT 重采样到 TARGET_SIZE
     */
    static LutData resample(const LutData& src, int targetSize = TARGET_SIZE);

    /**
     * LUT 索引辅助：(b, g, r) → float[3] 偏移
     */
    static inline int idx(int r, int g, int b, int size) {
        return (b * size * size + g * size + r) * 3;
    }
};

} // namespace reshade
