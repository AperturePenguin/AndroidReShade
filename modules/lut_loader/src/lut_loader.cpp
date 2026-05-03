/**
 * Module 3: LutLoader — 实现
 */
#include "lut_loader.h"
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <android/log.h>

// stb_image：单头文件 PNG 解码器（需要放在 third_party/stb/stb_image.h）
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

#define TAG "RS::LutLoader"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace reshade {

// ════════════════════════════════════════════════════════
// 公开接口
// ════════════════════════════════════════════════════════

GLuint LutLoader::load(const std::string& path) {
    LutData data = parse(path);
    if (!data.valid) {
        LOGE("LUT load failed: %s — %s", path.c_str(), data.errorLog.c_str());
        return 0;
    }
    return uploadToGPU(data);
}

LutData LutLoader::parse(const std::string& path) {
    std::string ext = path.substr(path.rfind('.') + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == "cube") return parseCube(path);
    if (ext == "png")  return parsePng(path);

    LutData err;
    err.errorLog = "Unsupported LUT extension: " + ext;
    return err;
}

// ════════════════════════════════════════════════════════
// .cube 解析
// ════════════════════════════════════════════════════════

LutData LutLoader::parseCube(const std::string& path) {
    LutData data;
    std::ifstream f(path);
    if (!f) { data.errorLog = "Cannot open: " + path; return data; }

    int lutSize = 0;
    std::string line;

    while (std::getline(f, line)) {
        // trim 前导空白
        size_t s = line.find_first_not_of(" \t\r");
        if (s == std::string::npos || line[s] == '#') continue;
        line = line.substr(s);

        // LUT_3D_SIZE
        if (line.substr(0, 11) == "LUT_3D_SIZE") {
            std::istringstream ss(line); std::string k;
            ss >> k >> lutSize;
            data.rgb.reserve(lutSize * lutSize * lutSize * 3);
            continue;
        }
        // 跳过元数据行
        if (line.substr(0,5) == "TITLE" ||
            line.substr(0,6) == "DOMAIN" ||
            line.substr(0,8) == "LUT_1D_S") continue;

        // 数据行：三个浮点数
        float r, g, b;
        std::istringstream ss(line);
        if (ss >> r >> g >> b) {
            data.rgb.push_back(r);
            data.rgb.push_back(g);
            data.rgb.push_back(b);
        }
    }

    if (lutSize == 0 && !data.rgb.empty())
        lutSize = (int)std::round(std::cbrt(data.rgb.size() / 3.0));

    if (data.rgb.size() != (size_t)(lutSize * lutSize * lutSize * 3)) {
        data.errorLog = "CUBE data size mismatch";
        return data;
    }

    data.size  = lutSize;
    data.valid = true;
    LOGI("CUBE parsed: size=%d  entries=%zu", lutSize, data.rgb.size()/3);
    return data;
}

// ════════════════════════════════════════════════════════
// .png 解析（Hald CLUT 或条带格式）
// ════════════════════════════════════════════════════════

LutData LutLoader::parsePng(const std::string& path) {
    LutData data;

    // 读文件到内存
    std::ifstream f(path, std::ios::binary);
    if (!f) { data.errorLog = "Cannot open: " + path; return data; }
    std::vector<uint8_t> buf(std::istreambuf_iterator<char>(f), {});

    int w, h, comp;
    uint8_t* pixels = stbi_load_from_memory(buf.data(), (int)buf.size(),
                                             &w, &h, &comp, 3);
    if (!pixels) {
        data.errorLog = std::string("stbi: ") + stbi_failure_reason();
        return data;
    }
    LOGI("PNG LUT: %dx%d comp=%d", w, h, comp);

    int lutSize = 0;

    // ── Hald CLUT（512×512，level-8）─────────────────────
    // Hald level-N: size = N*N,  image = size×size
    // For N=8: size=64, image=64×64 (实际 ReShade 常用 512=8³)
    if (w == h) {
        // 尝试推断 level
        // level^3 == w*h → level = (w*h)^(1/3)
        lutSize = (int)std::round(std::cbrt((double)w * h));
        if (lutSize * lutSize * lutSize != w * h) {
            // fallback：取 h 为边长（条带格式）
            lutSize = h;
        }

        data.rgb.resize(lutSize * lutSize * lutSize * 3);

        // 从 Hald 格式读取
        // Hald: pixel(x,y) = LUT entry at index y*w + x
        // index = b*N*N + g*N + r  (where N=lutSize)
        for (int b = 0; b < lutSize; ++b) {
        for (int g = 0; g < lutSize; ++g) {
        for (int r = 0; r < lutSize; ++r) {
            int linearIdx = b * lutSize * lutSize + g * lutSize + r;
            int px = linearIdx % w;
            int py = linearIdx / w;
            if (px >= w || py >= h) continue;
            int srcIdx = (py * w + px) * 3;
            int dstIdx = idx(r, g, b, lutSize);
            data.rgb[dstIdx+0] = pixels[srcIdx+0] / 255.f;
            data.rgb[dstIdx+1] = pixels[srcIdx+1] / 255.f;
            data.rgb[dstIdx+2] = pixels[srcIdx+2] / 255.f;
        }}}
    }
    // ── 条带格式（w = N*N, h = N）─────────────────────────
    else {
        lutSize = h;
        data.rgb.resize(lutSize * lutSize * lutSize * 3);

        for (int b = 0; b < lutSize; ++b) {
        for (int g = 0; g < lutSize; ++g) {
        for (int r = 0; r < lutSize; ++r) {
            int px     = b * lutSize + r;
            int py     = g;
            if (px >= w || py >= h) continue;
            int srcIdx = (py * w + px) * 3;
            int dstIdx = idx(r, g, b, lutSize);
            data.rgb[dstIdx+0] = pixels[srcIdx+0] / 255.f;
            data.rgb[dstIdx+1] = pixels[srcIdx+1] / 255.f;
            data.rgb[dstIdx+2] = pixels[srcIdx+2] / 255.f;
        }}}
    }

    stbi_image_free(pixels);
    data.size  = lutSize;
    data.valid = true;
    LOGI("PNG LUT parsed: size=%d", lutSize);
    return data;
}

// ════════════════════════════════════════════════════════
// 三线性重采样
// ════════════════════════════════════════════════════════

LutData LutLoader::resample(const LutData& src, int targetSize) {
    if (src.size == targetSize) return src;

    LutData dst;
    dst.size  = targetSize;
    dst.valid = true;
    dst.rgb.resize(targetSize * targetSize * targetSize * 3);

    const int N = src.size;

    for (int b2 = 0; b2 < targetSize; ++b2) {
    for (int g2 = 0; g2 < targetSize; ++g2) {
    for (int r2 = 0; r2 < targetSize; ++r2) {
        // 映射到原始 LUT 坐标
        float rf = r2 / (float)(targetSize-1) * (N-1);
        float gf = g2 / (float)(targetSize-1) * (N-1);
        float bf = b2 / (float)(targetSize-1) * (N-1);

        int ri = std::min((int)rf, N-2);
        int gi = std::min((int)gf, N-2);
        int bi = std::min((int)bf, N-2);

        float fr = rf - ri, fg = gf - gi, fb = bf - bi;

        // 三线性插值
        for (int ch = 0; ch < 3; ++ch) {
            float v =
                src.rgb[idx(ri,   gi,   bi,   N) + ch] * (1-fr)*(1-fg)*(1-fb)
              + src.rgb[idx(ri+1, gi,   bi,   N) + ch] * fr*(1-fg)*(1-fb)
              + src.rgb[idx(ri,   gi+1, bi,   N) + ch] * (1-fr)*fg*(1-fb)
              + src.rgb[idx(ri+1, gi+1, bi,   N) + ch] * fr*fg*(1-fb)
              + src.rgb[idx(ri,   gi,   bi+1, N) + ch] * (1-fr)*(1-fg)*fb
              + src.rgb[idx(ri+1, gi,   bi+1, N) + ch] * fr*(1-fg)*fb
              + src.rgb[idx(ri,   gi+1, bi+1, N) + ch] * (1-fr)*fg*fb
              + src.rgb[idx(ri+1, gi+1, bi+1, N) + ch] * fr*fg*fb;
            dst.rgb[idx(r2, g2, b2, targetSize) + ch] = v;
        }
    }}}

    LOGI("Resampled LUT %d → %d", src.size, targetSize);
    return dst;
}

// ════════════════════════════════════════════════════════
// 上传 GL_TEXTURE_3D
// ════════════════════════════════════════════════════════

GLuint LutLoader::uploadToGPU(const LutData& rawData) {
    if (!rawData.valid) return 0;

    // 统一重采样到 TARGET_SIZE
    LutData data = (rawData.size != TARGET_SIZE)
                 ? resample(rawData, TARGET_SIZE) : rawData;

    const int N = data.size;

    // float → RGBA8
    std::vector<uint8_t> rgba(N * N * N * 4);
    for (int i = 0; i < N * N * N; ++i) {
        rgba[i*4+0] = (uint8_t)std::min(255.f, data.rgb[i*3+0] * 255.f + 0.5f);
        rgba[i*4+1] = (uint8_t)std::min(255.f, data.rgb[i*3+1] * 255.f + 0.5f);
        rgba[i*4+2] = (uint8_t)std::min(255.f, data.rgb[i*3+2] * 255.f + 0.5f);
        rgba[i*4+3] = 255;
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_3D, tex);

    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8,
                 N, N, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_3D, 0);
    LOGI("LUT uploaded: id=%u size=%d^3", tex, N);
    return tex;
}

} // namespace reshade
