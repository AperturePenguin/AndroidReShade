#pragma once
/**
 * Module 7: ConfigSystem
 * ───────────────────────
 * 职责：Preset 保存 / 加载 / 管理
 *
 * 功能：
 *   - 将当前所有 Effect 参数序列化为 INI 格式（兼容 ReShade preset）
 *   - 从文件加载 preset 并还原参数
 *   - 支持多 preset 切换
 *   - 一键启用/禁用全部滤镜
 *   - 记录上次使用的 preset
 *
 * 文件格式（.ini）示例：
 *   [Effects]
 *   Enabled=Levels.fx,LUT.fx
 *   Disabled=CAS.fx
 *
 *   [Levels.fx#0]         # technique index
 *   Enabled=1
 *   Gamma=1.2
 *   BlackPoint=0.0
 *   WhitePoint=1.0
 */
#include <string>
#include <vector>
#include <unordered_map>

namespace reshade {

// ════════════════════════════════════════════════════════
// Preset 数据结构
// ════════════════════════════════════════════════════════

/** 单个 Effect 的参数快照 */
struct EffectSnapshot {
    std::string name;           // Effect 名（文件名不含路径）
    bool        enabled = true;
    std::unordered_map<std::string, float> params;  // uniformName → value
};

/** 完整 Preset */
struct Preset {
    std::string              name;          // preset 名称
    std::string              path;          // 存储路径（.ini）
    bool                     globalEnabled = true;
    std::vector<EffectSnapshot> effects;

    // 快捷查询
    const EffectSnapshot* findEffect(const std::string& effectName) const;
    EffectSnapshot*       findEffect(const std::string& effectName);
};

// ════════════════════════════════════════════════════════
// ConfigSystem 接口
// ════════════════════════════════════════════════════════

class ConfigSystem {
public:
    static constexpr const char* DEFAULT_DIR = "/sdcard/ReShade/presets/";

    ConfigSystem();

    // ── Preset 管理 ──────────────────────────────────────

    /** 从 .ini 文件加载 preset */
    Preset loadPreset(const std::string& path);

    /** 将 preset 保存到 .ini 文件 */
    bool savePreset(const Preset& preset, const std::string& path = "");

    /** 扫描 preset 目录，返回所有 .ini 文件路径 */
    std::vector<std::string> listPresets(const std::string& dir = DEFAULT_DIR);

    /** 删除 preset 文件 */
    bool deletePreset(const std::string& path);

    // ── 当前 Preset 状态 ─────────────────────────────────

    void        setCurrentPreset(Preset p);
    const Preset& currentPreset()  const { return current_; }
    Preset&       currentPreset()        { return current_; }

    /** 应用当前 preset 到 ShaderRuntime（通过回调） */
    using ApplyCallback = std::function<void(const EffectSnapshot& snap)>;
    void applyTo(const ApplyCallback& cb) const;

    // ── 参数增量更新 ─────────────────────────────────────

    /** 更新当前 preset 中某 Effect 的某个参数 */
    void updateParam(const std::string& effectName,
                     const std::string& paramName,
                     float value);

    /** 设置某 Effect 的启用状态 */
    void setEffectEnabled(const std::string& effectName, bool on);

    /** 全局开关 */
    void setGlobalEnabled(bool on);
    bool isGlobalEnabled() const { return current_.globalEnabled; }

    // ── 自动保存 ─────────────────────────────────────────

    /** 设置自动保存路径（参数变化时自动保存） */
    void enableAutoSave(const std::string& path, int intervalMs = 2000);
    void disableAutoSave();

    // ── 记忆最近使用 ─────────────────────────────────────

    void        saveLastUsed(const std::string& presetPath);
    std::string loadLastUsed();

private:
    Preset current_;
    std::string autoSavePath_;
    bool        autoSaveEnabled_ = false;

    // ── INI 解析 / 序列化 ─────────────────────────────────
    Preset parseIni (const std::string& content, const std::string& path);
    std::string serializeIni(const Preset& preset);

    // ── 自动保存定时器 ────────────────────────────────────
    void scheduleAutoSave();

    // ── 最近配置文件路径 ──────────────────────────────────
    static constexpr const char* LAST_USED_FILE = "/data/local/tmp/reshade_last_preset.txt";
};

} // namespace reshade
