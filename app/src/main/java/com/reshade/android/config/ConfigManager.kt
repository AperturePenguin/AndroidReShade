package com.reshade.android.config

import android.content.Context
import android.util.Log
import java.io.File
import java.text.SimpleDateFormat
import java.util.*

/**
 * Module 7: ConfigSystem — Kotlin 层
 *
 * 与 C++ ConfigSystem 使用相同的 .ini 格式，
 * 负责 Preset 的 UI 展示和文件管理。
 */

// ════════════════════════════════════════════════════════
// 数据模型
// ════════════════════════════════════════════════════════

data class EffectConfig(
    val name:    String,
    var enabled: Boolean = true,
    val params:  MutableMap<String, Float> = mutableMapOf()
)

data class Preset(
    var name:          String,
    var path:          String = "",
    var globalEnabled: Boolean = true,
    val effects:       MutableList<EffectConfig> = mutableListOf()
) {
    fun findEffect(name: String) = effects.firstOrNull { it.name == name }
    fun getOrCreate(name: String) =
        findEffect(name) ?: EffectConfig(name).also { effects.add(it) }
}

// ════════════════════════════════════════════════════════
// ConfigManager
// ════════════════════════════════════════════════════════

class ConfigManager(private val context: Context) {

    companion object {
        const val PRESET_DIR = "/sdcard/ReShade/presets/"
        const val LAST_USED  = "/data/local/tmp/reshade_last_preset.txt"
        private const val TAG = "RS::ConfigManager"
    }

    var current: Preset = Preset("Default")
        private set

    // ── 初始化 ────────────────────────────────────────────

    fun init() {
        File(PRESET_DIR).mkdirs()
        val last = loadLastUsed()
        if (last.isNotEmpty() && File(last).exists()) {
            load(last)?.let { current = it }
        }
    }

    // ── 加载 ─────────────────────────────────────────────

    fun load(path: String): Preset? {
        return try {
            val text = File(path).readText()
            parseIni(text, path).also {
                current = it
                saveLastUsed(path)
                Log.i(TAG, "Loaded preset: $path (${it.effects.size} effects)")
            }
        } catch (e: Exception) {
            Log.e(TAG, "Load failed: $path — ${e.message}")
            null
        }
    }

    // ── 保存 ─────────────────────────────────────────────

    fun save(preset: Preset = current, path: String = preset.path): Boolean {
        return try {
            val savePath = path.ifEmpty { "$PRESET_DIR${preset.name}.ini" }
            File(savePath).writeText(serializeIni(preset))
            preset.path = savePath
            saveLastUsed(savePath)
            Log.i(TAG, "Saved preset: $savePath")
            true
        } catch (e: Exception) {
            Log.e(TAG, "Save failed: ${e.message}")
            false
        }
    }

    /** 另存为新 Preset */
    fun saveAs(name: String): Boolean {
        val copy = current.copy(name = name, path = "$PRESET_DIR$name.ini")
        return save(copy)
    }

    // ── 列表 & 删除 ────────────────────────────────────────

    fun listPresets(): List<Preset> {
        return File(PRESET_DIR).listFiles()
            ?.filter { it.extension == "ini" }
            ?.mapNotNull { f ->
                try { parseIni(f.readText(), f.absolutePath) }
                catch (e: Exception) { null }
            }
            ?.sortedBy { it.name }
            ?: emptyList()
    }

    fun delete(preset: Preset): Boolean {
        if (preset.path.isEmpty()) return false
        return File(preset.path).delete()
    }

    // ── 参数更新 ─────────────────────────────────────────

    fun updateParam(effectName: String, paramName: String, value: Float) {
        current.getOrCreate(effectName).params[paramName] = value
        scheduleAutoSave()
    }

    fun setEffectEnabled(effectName: String, on: Boolean) {
        current.getOrCreate(effectName).enabled = on
        scheduleAutoSave()
    }

    fun setGlobalEnabled(on: Boolean) {
        current.globalEnabled = on
        scheduleAutoSave()
    }

    // ── INI 解析 ──────────────────────────────────────────

    private fun parseIni(text: String, path: String): Preset {
        val preset = Preset("Unnamed", path)
        var currentSection = ""
        var currentEffect: EffectConfig? = null

        for (raw in text.lines()) {
            val line = raw.trim()
            if (line.isEmpty() || line.startsWith(";") || line.startsWith("#")) continue

            if (line.startsWith("[") && line.endsWith("]")) {
                currentSection = line.drop(1).dropLast(1)
                if (currentSection != "Effects") {
                    val effectName = currentSection.substringBefore("#")
                    currentEffect = preset.getOrCreate(effectName)
                }
                continue
            }

            val eqIdx = line.indexOf('=')
            if (eqIdx < 0) continue
            val key = line.substring(0, eqIdx).trim()
            val value = line.substring(eqIdx + 1).trim()

            when {
                currentSection == "Effects" -> {
                    when (key) {
                        "GlobalEnabled" -> preset.globalEnabled = value == "1" || value == "true"
                        "Enabled"       -> value.split(",").forEach { n ->
                            n.trim().takeIf { it.isNotEmpty() }
                                ?.let { preset.getOrCreate(it).enabled = true }
                        }
                        "Disabled"      -> value.split(",").forEach { n ->
                            n.trim().takeIf { it.isNotEmpty() }
                                ?.let { preset.getOrCreate(it).enabled = false }
                        }
                        "PresetName"    -> preset.name = value
                    }
                }
                currentEffect != null -> {
                    when (key) {
                        "Enabled" -> currentEffect!!.enabled = value == "1" || value == "true"
                        else      -> value.toFloatOrNull()?.let { currentEffect!!.params[key] = it }
                    }
                }
            }
        }

        if (preset.name == "Unnamed")
            preset.name = path.substringAfterLast("/").substringBeforeLast(".")

        return preset
    }

    // ── INI 序列化 ────────────────────────────────────────

    private fun serializeIni(preset: Preset): String = buildString {
        appendLine("; AndroidReShade Preset")
        appendLine("; Generated: ${SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(Date())}")
        appendLine()

        appendLine("[Effects]")
        appendLine("PresetName=${preset.name}")
        appendLine("GlobalEnabled=${if (preset.globalEnabled) "1" else "0"}")
        append("Enabled=")
        appendLine(preset.effects.filter { it.enabled }.joinToString(",") { it.name })
        append("Disabled=")
        appendLine(preset.effects.filterNot { it.enabled }.joinToString(",") { it.name })
        appendLine()

        preset.effects.forEachIndexed { i, e ->
            appendLine("[${e.name}#$i]")
            appendLine("Enabled=${if (e.enabled) "1" else "0"}")
            e.params.forEach { (k, v) ->
                appendLine("$k=$v")
            }
            appendLine()
        }
    }

    // ── 自动保存（防抖）─────────────────────────────────

    private var autoSaveThread: Thread? = null

    private fun scheduleAutoSave() {
        autoSaveThread?.interrupt()
        val snapshot = current.copy(
            effects = current.effects.map { it.copy(params = it.params.toMutableMap()) }.toMutableList()
        )
        autoSaveThread = Thread {
            try {
                Thread.sleep(1500)
                save(snapshot)
            } catch (_: InterruptedException) {}
        }.also { it.start() }
    }

    // ── 最近使用 ─────────────────────────────────────────

    private fun saveLastUsed(path: String) {
        try { File(LAST_USED).writeText(path) } catch (_: Exception) {}
    }

    private fun loadLastUsed(): String {
        return try { File(LAST_USED).readText().trim() } catch (_: Exception) { "" }
    }
}
