/**
 * Module 7: ConfigSystem — 实现
 */
#include "config_system.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <thread>
#include <chrono>
#include <mutex>
#include <android/log.h>

#define TAG "RS::ConfigSystem"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace reshade {

// ════════════════════════════════════════════════════════
// Preset 辅助
// ════════════════════════════════════════════════════════

const EffectSnapshot* Preset::findEffect(const std::string& name) const {
    for (auto& e : effects)
        if (e.name == name) return &e;
    return nullptr;
}
EffectSnapshot* Preset::findEffect(const std::string& name) {
    for (auto& e : effects)
        if (e.name == name) return &e;
    return nullptr;
}

// ════════════════════════════════════════════════════════
// ConfigSystem
// ════════════════════════════════════════════════════════

ConfigSystem::ConfigSystem() {
    // 创建默认目录
    std::filesystem::create_directories(DEFAULT_DIR);

    // 尝试恢复上次 preset
    auto last = loadLastUsed();
    if (!last.empty()) {
        auto p = loadPreset(last);
        if (!p.path.empty()) {
            current_ = p;
            LOGI("Restored last preset: %s", last.c_str());
        }
    }
}

// ════════════════════════════════════════════════════════
// 加载 .ini
// ════════════════════════════════════════════════════════

Preset ConfigSystem::loadPreset(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        LOGE("Cannot open preset: %s", path.c_str());
        return {};
    }
    std::string content((std::istreambuf_iterator<char>(f)), {});
    auto preset = parseIni(content, path);
    LOGI("Loaded preset: %s  effects=%zu", path.c_str(), preset.effects.size());
    return preset;
}

// ── INI 解析 ────────────────────────────────────────────
// 支持格式：
//   [Effects]
//   Enabled=Levels.fx,LUT.fx
//
//   [Levels.fx#0]
//   Enabled=1
//   Gamma=1.2
//   BlackPoint=0.0
//
Preset ConfigSystem::parseIni(const std::string& content,
                                const std::string& path) {
    Preset preset;
    preset.path = path;
    preset.name = path.substr(path.find_last_of("/\\") + 1);
    // 去掉扩展名
    auto dotPos = preset.name.rfind('.');
    if (dotPos != std::string::npos) preset.name = preset.name.substr(0, dotPos);

    std::istringstream ss(content);
    std::string line;
    std::string currentSection;
    EffectSnapshot* currentEffect = nullptr;

    while (std::getline(ss, line)) {
        // trim
        line.erase(0, line.find_first_not_of(" \t\r"));
        line.erase(line.find_last_not_of(" \t\r") + 1);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        // 节标题 [SectionName]
        if (line.front() == '[' && line.back() == ']') {
            currentSection = line.substr(1, line.size() - 2);
            if (currentSection != "Effects") {
                // 格式：EffectName.fx#0 或 EffectName
                std::string effectName = currentSection;
                auto sharp = effectName.rfind('#');
                if (sharp != std::string::npos)
                    effectName = effectName.substr(0, sharp);
                // 查找或创建
                currentEffect = preset.findEffect(effectName);
                if (!currentEffect) {
                    preset.effects.push_back({effectName, true, {}});
                    currentEffect = &preset.effects.back();
                }
            }
            continue;
        }

        // 键值对
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        // trim
        key.erase(key.find_last_not_of(" \t") + 1);
        val.erase(0, val.find_first_not_of(" \t"));

        if (currentSection == "Effects") {
            if (key == "GlobalEnabled" || key == "TechniquesEnabled") {
                preset.globalEnabled = (val == "1" || val == "true");
            } else if (key == "Enabled") {
                // 逗号分隔的 Effect 名列表
                std::istringstream names(val);
                std::string n;
                while (std::getline(names, n, ',')) {
                    n.erase(0, n.find_first_not_of(" "));
                    n.erase(n.find_last_not_of(" ") + 1);
                    if (n.empty()) continue;
                    auto* e = preset.findEffect(n);
                    if (!e) { preset.effects.push_back({n, true, {}}); }
                    else e->enabled = true;
                }
            } else if (key == "Disabled") {
                std::istringstream names(val);
                std::string n;
                while (std::getline(names, n, ',')) {
                    n.erase(0, n.find_first_not_of(" "));
                    n.erase(n.find_last_not_of(" ") + 1);
                    if (n.empty()) continue;
                    auto* e = preset.findEffect(n);
                    if (!e) { preset.effects.push_back({n, false, {}}); }
                    else e->enabled = false;
                }
            }
        } else if (currentEffect) {
            if (key == "Enabled") {
                currentEffect->enabled = (val == "1" || val == "true");
            } else {
                try {
                    currentEffect->params[key] = std::stof(val);
                } catch (...) {}
            }
        }
    }

    return preset;
}

// ════════════════════════════════════════════════════════
// 保存 .ini
// ════════════════════════════════════════════════════════

bool ConfigSystem::savePreset(const Preset& preset, const std::string& pathOverride) {
    std::string savePath = pathOverride.empty() ? preset.path : pathOverride;
    if (savePath.empty())
        savePath = std::string(DEFAULT_DIR) + preset.name + ".ini";

    std::ofstream f(savePath);
    if (!f) { LOGE("Cannot write preset: %s", savePath.c_str()); return false; }

    f << serializeIni(preset);
    LOGI("Saved preset: %s", savePath.c_str());
    return true;
}

std::string ConfigSystem::serializeIni(const Preset& preset) {
    std::ostringstream out;

    // [Effects] 节
    out << "[Effects]\n";
    out << "GlobalEnabled=" << (preset.globalEnabled ? "1" : "0") << "\n";

    // 已启用列表
    out << "Enabled=";
    bool first = true;
    for (auto& e : preset.effects) {
        if (!e.enabled) continue;
        if (!first) out << ","; first = false;
        out << e.name;
    }
    out << "\n";

    // 已禁用列表
    out << "Disabled=";
    first = true;
    for (auto& e : preset.effects) {
        if (e.enabled) continue;
        if (!first) out << ","; first = false;
        out << e.name;
    }
    out << "\n\n";

    // 每个 Effect 的参数
    for (size_t i = 0; i < preset.effects.size(); ++i) {
        auto& e = preset.effects[i];
        out << "[" << e.name << "#" << i << "]\n";
        out << "Enabled=" << (e.enabled ? "1" : "0") << "\n";
        for (auto& [k, v] : e.params) {
            out << k << "=" << v << "\n";
        }
        out << "\n";
    }
    return out.str();
}

// ════════════════════════════════════════════════════════
// 列表 & 删除
// ════════════════════════════════════════════════════════

std::vector<std::string> ConfigSystem::listPresets(const std::string& dir) {
    std::vector<std::string> result;
    try {
        for (auto& entry : std::filesystem::directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".ini") result.push_back(entry.path().string());
        }
    } catch (...) {}
    std::sort(result.begin(), result.end());
    return result;
}

bool ConfigSystem::deletePreset(const std::string& path) {
    return std::filesystem::remove(path);
}

// ════════════════════════════════════════════════════════
// 当前 Preset 操作
// ════════════════════════════════════════════════════════

void ConfigSystem::setCurrentPreset(Preset p) {
    current_ = std::move(p);
    if (autoSaveEnabled_) scheduleAutoSave();
}

void ConfigSystem::applyTo(const ApplyCallback& cb) const {
    if (!current_.globalEnabled) return;
    for (auto& e : current_.effects)
        if (e.enabled) cb(e);
}

void ConfigSystem::updateParam(const std::string& effectName,
                                const std::string& paramName,
                                float value) {
    auto* e = current_.findEffect(effectName);
    if (!e) {
        current_.effects.push_back({effectName, true, {}});
        e = &current_.effects.back();
    }
    e->params[paramName] = value;
    if (autoSaveEnabled_) scheduleAutoSave();
}

void ConfigSystem::setEffectEnabled(const std::string& name, bool on) {
    auto* e = current_.findEffect(name);
    if (!e) { current_.effects.push_back({name, on, {}}); return; }
    e->enabled = on;
    if (autoSaveEnabled_) scheduleAutoSave();
}

void ConfigSystem::setGlobalEnabled(bool on) {
    current_.globalEnabled = on;
    if (autoSaveEnabled_) scheduleAutoSave();
}

// ════════════════════════════════════════════════════════
// 自动保存
// ════════════════════════════════════════════════════════

static std::mutex   g_saveMutex;
static std::thread* g_saveThread = nullptr;
static bool         g_savePending = false;

void ConfigSystem::enableAutoSave(const std::string& path, int intervalMs) {
    autoSavePath_    = path;
    autoSaveEnabled_ = true;
}

void ConfigSystem::disableAutoSave() {
    autoSaveEnabled_ = false;
}

void ConfigSystem::scheduleAutoSave() {
    std::lock_guard<std::mutex> lk(g_saveMutex);
    g_savePending = true;
    if (g_saveThread && g_saveThread->joinable()) return;

    auto snapshot = current_;
    auto savePath = autoSavePath_;

    delete g_saveThread;
    g_saveThread = new std::thread([this, snapshot, savePath]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        std::lock_guard<std::mutex> lk(g_saveMutex);
        if (g_savePending) {
            savePreset(snapshot, savePath);
            g_savePending = false;
        }
    });
    g_saveThread->detach();
}

// ════════════════════════════════════════════════════════
// 最近使用记录
// ════════════════════════════════════════════════════════

void ConfigSystem::saveLastUsed(const std::string& presetPath) {
    std::ofstream f(LAST_USED_FILE);
    if (f) f << presetPath;
}

std::string ConfigSystem::loadLastUsed() {
    std::ifstream f(LAST_USED_FILE);
    if (!f) return {};
    std::string path;
    std::getline(f, path);
    return path;
}

} // namespace reshade
