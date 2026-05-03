package com.reshade.android.config

import android.content.Context
import android.widget.*
import android.view.*
import android.app.AlertDialog
import com.reshade.android.R
import com.reshade.android.ipc.IpcClient
import com.reshade.android.ipc.IpcMessage

/**
 * Preset 管理 UI
 * 提供：列表选择、保存、另存为、删除、全局开关
 */
class PresetManagerUI(
    private val context: Context,
    private val configManager: ConfigManager,
    private val ipc: IpcClient
) {
    /** 显示 Preset 管理对话框 */
    fun show() {
        val presets = configManager.listPresets()
        val names   = presets.map { it.name }.toTypedArray()

        val builder = AlertDialog.Builder(context)
        builder.setTitle("🎨 Preset 管理")

        val view = LayoutInflater.from(context).inflate(R.layout.dialog_preset_manager, null)
        builder.setView(view)

        val listView = view.findViewById<ListView>(R.id.lvPresets)
        val adapter  = ArrayAdapter(context, android.R.layout.simple_list_item_single_choice, names)
        listView.adapter       = adapter
        listView.choiceMode    = ListView.CHOICE_MODE_SINGLE

        // 标记当前选中
        val currentName = configManager.current.name
        val idx = presets.indexOfFirst { it.name == currentName }
        if (idx >= 0) listView.setItemChecked(idx, true)

        builder.setPositiveButton("应用") { _, _ ->
            val sel = listView.checkedItemPosition
            if (sel >= 0) applyPreset(presets[sel])
        }
        builder.setNeutralButton("另存为") { _, _ -> showSaveAsDialog() }
        builder.setNegativeButton("取消", null)

        // 长按删除
        val dialog = builder.create()
        listView.setOnItemLongClickListener { _, _, pos, _ ->
            AlertDialog.Builder(context)
                .setTitle("删除 Preset")
                .setMessage("确认删除「${presets[pos].name}」？")
                .setPositiveButton("删除") { _, _ ->
                    configManager.delete(presets[pos])
                    dialog.dismiss()
                    show()  // 刷新
                }
                .setNegativeButton("取消", null)
                .show()
            true
        }
        dialog.show()
    }

    private fun applyPreset(preset: Preset) {
        configManager.load(preset.path)
        // 通知注入层加载所有 Effect 参数
        configManager.current.effects.forEach { e ->
            ipc.send(IpcMessage.ToggleEffect(-1, configManager.current.globalEnabled))
            e.params.forEach { (name, value) ->
                ipc.send(IpcMessage.SetUniform(-1, name, value))
            }
        }
        Toast.makeText(context, "已应用: ${preset.name}", Toast.LENGTH_SHORT).show()
    }

    private fun showSaveAsDialog() {
        val et = EditText(context)
        et.hint = "输入 Preset 名称"
        et.setText(configManager.current.name)

        AlertDialog.Builder(context)
            .setTitle("另存为")
            .setView(et)
            .setPositiveButton("保存") { _, _ ->
                val name = et.text.toString().trim()
                if (name.isNotEmpty()) {
                    configManager.saveAs(name)
                    Toast.makeText(context, "已保存: $name", Toast.LENGTH_SHORT).show()
                }
            }
            .setNegativeButton("取消", null)
            .show()
    }
}
