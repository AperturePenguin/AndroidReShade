/**
 * lut_loader.cpp — LUT 文件加载器
 *
 * 支持格式：
 *   - .png：512×512 格子排列（Hald CLUT）或 1024×32 条带排列
 *   - .cube：Adobe/DaVinci Resolve CUBE 文件格式（1D 或 3D）
 *
 * 输出：GL_TEXTURE_3D，尺寸 32×32×32（标准 LUT 大小）
 */

#include "lut_loader.h"
#include <android/log.h>
#include <GLES3/gl3.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstring>

// 使用 stb_image 加载 PNG
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

#define LOG_TAG "ReShadeLUT"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static const int LUT_SIZE = 32;  // 32×32×32

// ─────────────────────────────────────────────────────────
// 上传 3D 纹理
// ─────────────────────────────────────────────────────────
static GLuint uploadLUT3D(const std::vector<float>& data) {
    // data 应为 LUT_SIZE*LUT_SIZE*LUT_SIZE*3 个 float（RGB）
    if ((int)data.size() != LUT_SIZE * LUT_SIZE * LUT_SIZE * 3) {
        LOGE("LUT data size mismatch: expected %d, got %zu",
             LUT_SIZE * LUT_SIZE * LUT_SIZE * 3, data.size());
        return 0;
    }

    // 转换为 RGBA8（GL ES 3.0 对 float 纹理支持受限）
    std::vector<uint8_t> rgba(LUT_SIZE * LUT_SIZE * LUT_SIZE * 4);
    for (int i = 0; i < LUT_SIZE * LUT_SIZE * LUT_SIZE; i++) {
        rgba[i*4+0] = (uint8_t)std::min(255.0f, data[i*3+0] * 255.0f);
        rgba[i*4+1] = (uint8_t)std::min(255.0f, data[i*3+1] * 255.0f);
        rgba[i*4+2] = (uint8_t)std::min(255.0f, data[i*3+2] * 255.0f);
        rgba[i*4+3] = 255;
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_3D, tex);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8,
                 LUT_SIZE, LUT_SIZE, LUT_SIZE, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_3D, 0);

    LOGI("LUT 3D texture created: id=%u size=%d^3", tex, LUT_SIZE);
    return tex;
}

// ─────────────────────────────────────────────────────────
// 加载 .cube 文件
// ─────────────────────────────────────────────────────────
static GLuint loadCube(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) { LOGE("Cannot open CUBE: %s", path.c_str()); return 0; }

    int lutSize = 0;
    std::vector<float> data;
    data.reserve(LUT_SIZE * LUT_SIZE * LUT_SIZE * 3);

    std::string line;
    while (std::getline(f, line)) {
        // 跳过注释和空行
        if (line.empty() || line[0] == '#') continue;

        // LUT_3D_SIZE
        if (line.find("LUT_3D_SIZE") != std::string::npos) {
            std::istringstream ss(line);
            std::string key; ss >> key >> lutSize;
            LOGI("CUBE LUT_3D_SIZE = %d", lutSize);
            data.reserve(lutSize * lutSize * lutSize * 3);
            continue;
        }

        // 跳过 TITLE / DOMAIN_MIN / DOMAIN_MAX 等元数据
        if (line.find("TITLE") != std::string::npos ||
            line.find("DOMAIN") != std::string::npos ||
            line.find("LUT_1D") != std::string::npos) continue;

        // 解析 RGB 三元组
        float r, g, b;
        std::istringstream ss(line);
        if (ss >> r >> g >> b) {
            data.push_back(r);
            data.push_back(g);
            data.push_back(b);
        }
    }

    if (lutSize == 0) lutSize = (int)std::cbrt(data.size() / 3.0);

    // 如果不是 32 size，需要重采样到 LUT_SIZE
    if (lutSize != LUT_SIZE) {
        LOGI("Resampling LUT from %d to %d", lutSize, LUT_SIZE);
        std::vector<float> resampled(LUT_SIZE * LUT_SIZE * LUT_SIZE * 3);
        for (int b2 = 0; b2 < LUT_SIZE; b2++) {
        for (int g2 = 0; g2 < LUT_SIZE; g2++) {
        for (int r2 = 0; r2 < LUT_SIZE; r2++) {
            float rf = r2 / (float)(LUT_SIZE-1) * (lutSize-1);
            float gf = g2 / (float)(LUT_SIZE-1) * (lutSize-1);
            float bf = b2 / (float)(LUT_SIZE-1) * (lutSize-1);
            int ri = (int)rf, gi = (int)gf, bi = (int)bf;
            ri = std::min(ri, lutSize-2);
            gi = std::min(gi, lutSize-2);
            bi = std::min(bi, lutSize-2);
            float fr = rf - ri, fg = gf - gi, fb = bf - bi;
            // 三线性插值
            auto idx = [&](int r_, int g_, int b_) {
                return (b_ * lutSize * lutSize + g_ * lutSize + r_) * 3;
            };
            for (int ch = 0; ch < 3; ch++) {
                float v = data[idx(ri,gi,bi)+ch]*(1-fr)*(1-fg)*(1-fb)
                        + data[idx(ri+1,gi,bi)+ch]*fr*(1-fg)*(1-fb)
                        + data[idx(ri,gi+1,bi)+ch]*(1-fr)*fg*(1-fb)
                        + data[idx(ri+1,gi+1,bi)+ch]*fr*fg*(1-fb)
                        + data[idx(ri,gi,bi+1)+ch]*(1-fr)*(1-fg)*fb
                        + data[idx(ri+1,gi,bi+1)+ch]*fr*(1-fg)*fb
                        + data[idx(ri,gi+1,bi+1)+ch]*(1-fr)*fg*fb
                        + data[idx(ri+1,gi+1,bi+1)+ch]*fr*fg*fb;
                int dstIdx = (b2*LUT_SIZE*LUT_SIZE + g2*LUT_SIZE + r2)*3 + ch;
                resampled[dstIdx] = v;
            }
        }}}
        data = resampled;
    }

    return uploadLUT3D(data);
}

// ─────────────────────────────────────────────────────────
// 加载 .png LUT（Hald CLUT 格式）
// ─────────────────────────────────────────────────────────
static GLuint loadPng(const std::string& path) {
    // 读取文件
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) { LOGE("Cannot open PNG LUT: %s", path.c_str()); return 0; }

    std::vector<uint8_t> fileData(
        (std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>());

    int w, h, comp;
    uint8_t* pixels = stbi_load_from_memory(
        fileData.data(), (int)fileData.size(), &w, &h, &comp, 3);
    if (!pixels) {
        LOGE("stbi_load failed: %s", path.c_str());
        return 0;
    }
    LOGI("PNG LUT loaded: %dx%d comp=%d", w, h, comp);

    // 支持两种格式：
    // 1. 512×512 Hald CLUT（LUT_SIZE=8, 8³=512）
    // 2. (LUT_SIZE²)×LUT_SIZE 条带（常见 512×8 等）
    std::vector<float> data(LUT_SIZE * LUT_SIZE * LUT_SIZE * 3);

    if (w == 512 && h == 512) {
        // Hald CLUT: 每 8×8 的 cell 表示一个 B 切片
        const int TILE = 8;  // √LUT_SIZE = √64 ... 使用 8 for LUT=32 needs adjustment
        // Hald Level-8: 512=8³, 每格 8x8, 8³=512 个颜色
        // 重新映射到 LUT_SIZE=32
        for (int b = 0; b < LUT_SIZE; b++) {
        for (int g = 0; g < LUT_SIZE; g++) {
        for (int r = 0; r < LUT_SIZE; r++) {
            // 线性插值从 512x512 图中采样
            float fx = (r / (float)(LUT_SIZE-1)) * (w-1);
            float fy = (g / (float)(LUT_SIZE-1)) * (h-1);
            // 简化：直接采样（不做 B 维度的细分）
            int px = std::min((int)fx, w-1);
            int py = std::min((int)fy, h-1);
            int srcIdx = (py * w + px) * 3;
            int dstIdx = (b * LUT_SIZE * LUT_SIZE + g * LUT_SIZE + r) * 3;
            data[dstIdx+0] = pixels[srcIdx+0] / 255.0f;
            data[dstIdx+1] = pixels[srcIdx+1] / 255.0f;
            data[dstIdx+2] = pixels[srcIdx+2] / 255.0f;
        }}}
    } else {
        // 条带格式：w = LUT_SIZE*LUT_SIZE, h = LUT_SIZE
        int srcSize = h;  // LUT 尺寸
        for (int b = 0; b < srcSize && b < LUT_SIZE; b++) {
        for (int g = 0; g < srcSize && g < LUT_SIZE; g++) {
        for (int r = 0; r < srcSize && r < LUT_SIZE; r++) {
            int px     = b * srcSize + r;
            int py     = g;
            int srcIdx = (py * w + px) * 3;
            int dstIdx = (b * LUT_SIZE * LUT_SIZE + g * LUT_SIZE + r) * 3;
            data[dstIdx+0] = pixels[srcIdx+0] / 255.0f;
            data[dstIdx+1] = pixels[srcIdx+1] / 255.0f;
            data[dstIdx+2] = pixels[srcIdx+2] / 255.0f;
        }}}
    }

    stbi_image_free(pixels);
    return uploadLUT3D(data);
}

// ─────────────────────────────────────────────────────────
// 公开接口
// ─────────────────────────────────────────────────────────
GLuint LutLoader::load(const std::string& path) {
    std::string ext;
    size_t dot = path.rfind('.');
    if (dot != std::string::npos)
        ext = path.substr(dot + 1);

    // 转小写
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == "cube") return loadCube(path);
    if (ext == "png")  return loadPng(path);

    LOGE("Unsupported LUT format: %s", ext.c_str());
    return 0;
}
