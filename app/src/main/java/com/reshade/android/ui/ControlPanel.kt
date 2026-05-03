package com.reshade.android.ui

import android.content.Context
import android.graphics.PixelFormat
import android.os.Build
import android.view.*
import android.widget.*
import com.reshade.android.R
import com.reshade.android.ipc.IpcClient
import com.reshade.android.ipc.IpcMessage

/**
 * Module 6: ControlUI
 * ─────────────────────
 * 职责：悬浮窗参数控制面板
 *
 * 功能：
 *   - 动态生成 Slider 控件（根据 Effect 的 UniformDesc 列表）
 *   - 实时更新 shader uniform 参数
 *   - 支持折叠/展开、拖拽移动
 *   - 显示每个 Effect 的开关
 */
class ControlPanel(
    private val context: Context,
    private val ipc: IpcClient
) {
    // ── 数据模型 ──────────────────────────────────────────

    data class UniformSlider(
        val effectId:  Int,
        val name:      String,
        val label:     String,
        val min:       Float,
        val max:       Float,
        val step:      Float,
        var current:   Float,
        val uiType:    String   // "slider" | "color" | "checkbox"
    )

    data class EffectEntry(
        val id:       Int,
        val name:     String,
        var enabled:  Boolean,
        val sliders:  MutableList<UniformSlider> = mutableListOf()
    )

    // ── 状态 ─────────────────────────────────────────────
    private val effects = mutableListOf<EffectEntry>()
    private lateinit var root: View
    private lateinit var wm: WindowManager
    private lateinit var params: WindowManager.LayoutParams
    private var attached = false

    // ── 安装悬浮窗 ────────────────────────────────────────
    fun attach() {
        if (attached) return
        wm = context.getSystemService(Context.WINDOW_SERVICE) as WindowManager

        root = LayoutInflater.from(context).inflate(R.layout.overlay_control, null)
        params = buildLayoutParams()

        setupDragHandle()
        rebuildEffectList()

        wm.addView(root, params)
        attached = true
    }

    fun detach() {
        if (!attached) return
        wm.removeView(root)
        attached = false
    }

    // ── Effect 注册 ───────────────────────────────────────

    /** 注册一个 Effect 并指定其 uniform 列表 */
    fun registerEffect(
        id:       Int,
        name:     String,
        uniforms: List<UniformMeta>
    ) {
        val entry = EffectEntry(id, name, true)
        for (u in uniforms) {
            entry.sliders.add(
                UniformSlider(
                    effectId = id,
                    name     = u.name,
                    label    = u.label.ifEmpty { u.name },
                    min      = u.min,
                    max      = u.max,
                    step     = u.step,
                    current  = u.default,
                    uiType   = u.uiType
                )
            )
        }
        effects.removeIf { it.id == id }
        effects.add(entry)
        if (attached) rebuildEffectList()
    }

    fun unregisterEffect(id: Int) {
        effects.removeIf { it.id == id }
        if (attached) rebuildEffectList()
    }

    // ── UI 构建 ───────────────────────────────────────────

    private fun rebuildEffectList() {
        val container = root.findViewById<LinearLayout>(R.id.llEffects) ?: return
        container.removeAllViews()

        for (effect in effects) {
            val effectView = buildEffectView(effect)
            container.addView(effectView)
        }
    }

    private fun buildEffectView(effect: EffectEntry): View {
        val inflater = LayoutInflater.from(context)
        val card = inflater.inflate(R.layout.item_effect_card, null) as LinearLayout

        // 标题行
        val tvName    = card.findViewById<TextView>(R.id.tvEffectName)
        val swEnabled = card.findViewById<Switch>(R.id.swEffectEnable)
        val btnToggle = card.findViewById<Button>(R.id.btnCollapse)
        val sliderBox = card.findViewById<LinearLayout>(R.id.llSliders)

        tvName.text       = effect.name
        swEnabled.isChecked = effect.enabled

        swEnabled.setOnCheckedChangeListener { _, checked ->
            effect.enabled = checked
            ipc.send(IpcMessage.toggleEffect(effect.id, checked))
        }

        // 折叠/展开
        var collapsed = false
        btnToggle.setOnClickListener {
            collapsed = !collapsed
            sliderBox.visibility = if (collapsed) View.GONE else View.VISIBLE
            btnToggle.text = if (collapsed) "▶" else "▼"
        }

        // 为每个 uniform 生成控件
        for (slider in effect.sliders) {
            val row = when (slider.uiType) {
                "checkbox" -> buildCheckboxRow(slider)
                "color"    -> buildColorRow(slider)
                else       -> buildSliderRow(slider)
            }
            sliderBox.addView(row)
        }

        return card
    }

    private fun buildSliderRow(s: UniformSlider): View {
        val row = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(0, 4, 0, 4)
        }

        // 标签 + 数值
        val header = LinearLayout(context).apply {
            orientation = LinearLayout.HORIZONTAL
        }
        val tvLabel = TextView(context).apply {
            text     = s.label
            textSize = 11f
            setTextColor(0xFFAAAAAA.toInt())
            layoutParams = LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
        }
        val tvValue = TextView(context).apply {
            text     = formatValue(s.current)
            textSize = 11f
            setTextColor(0xFFFFFFFF.toInt())
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT)
        }
        header.addView(tvLabel)
        header.addView(tvValue)

        // SeekBar
        val steps = ((s.max - s.min) / s.step).toInt().coerceAtLeast(1)
        val seekBar = SeekBar(context).apply {
            max      = steps
            progress = ((s.current - s.min) / s.step).toInt().coerceIn(0, steps)
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT)
        }

        seekBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(sb: SeekBar, p: Int, fromUser: Boolean) {
                val v = s.min + p * s.step
                s.current = v
                tvValue.text = formatValue(v)
                if (fromUser)
                    ipc.send(IpcMessage.setUniform(s.effectId, s.name, v))
            }
            override fun onStartTrackingTouch(sb: SeekBar) {}
            override fun onStopTrackingTouch(sb: SeekBar) {}
        })

        row.addView(header)
        row.addView(seekBar)
        return row
    }

    private fun buildCheckboxRow(s: UniformSlider): View {
        val cb = CheckBox(context).apply {
            text      = s.label
            isChecked = s.current != 0f
            setTextColor(0xFFCCCCCC.toInt())
            textSize = 12f
        }
        cb.setOnCheckedChangeListener { _, checked ->
            s.current = if (checked) 1f else 0f
            ipc.send(IpcMessage.setUniform(s.effectId, s.name, s.current))
        }
        return cb
    }

    private fun buildColorRow(s: UniformSlider): View {
        // 简化版：3 个 RGB slider
        val col = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }
        val tvLabel = TextView(context).apply {
            text = s.label + " (RGB)"
            textSize = 11f
            setTextColor(0xFFAAAAAA.toInt())
        }
        col.addView(tvLabel)
        // 实际项目中这里应展开 R/G/B 三个 slider
        col.addView(buildSliderRow(s.copy(label = s.label + ".R")))
        return col
    }

    private fun formatValue(v: Float): String {
        return if (v % 1f == 0f) v.toInt().toString()
        else String.format("%.2f", v)
    }

    // ── 拖拽移动 ──────────────────────────────────────────

    private fun setupDragHandle() {
        val handle = root.findViewById<View>(R.id.dragHandle) ?: return
        var ix = 0; var iy = 0
        var tx = 0f; var ty = 0f

        handle.setOnTouchListener { _, e ->
            when (e.action) {
                MotionEvent.ACTION_DOWN -> {
                    ix = params.x; iy = params.y
                    tx = e.rawX;   ty = e.rawY
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    params.x = ix + (e.rawX - tx).toInt()
                    params.y = iy + (e.rawY - ty).toInt()
                    if (attached) wm.updateViewLayout(root, params)
                    true
                }
                else -> false
            }
        }
    }

    private fun buildLayoutParams() = WindowManager.LayoutParams(
        320.dpToPx(), WindowManager.LayoutParams.WRAP_CONTENT,
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
        else @Suppress("DEPRECATION")
            WindowManager.LayoutParams.TYPE_PHONE,
        WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE,
        PixelFormat.TRANSLUCENT
    ).also {
        it.gravity = Gravity.TOP or Gravity.START
        it.x = 16; it.y = 200
    }

    private fun Int.dpToPx() =
        (this * context.resources.displayMetrics.density).toInt()

    // ── 元数据（从 C++ 层通过 JNI 传入）──────────────────

    data class UniformMeta(
        val name:    String,
        val label:   String = "",
        val uiType:  String = "slider",
        val min:     Float  = 0f,
        val max:     Float  = 1f,
        val step:    Float  = 0.01f,
        val default: Float  = 0f
    )
}
