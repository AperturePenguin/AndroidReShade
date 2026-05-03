package com.reshade.android

import android.app.*
import android.content.Context
import android.content.Intent
import android.graphics.PixelFormat
import android.os.*
import android.view.*
import android.widget.*
import androidx.core.app.NotificationCompat

/**
 * OverlayService — 悬浮窗参数调节面板
 *
 * 显示一个可拖拽的悬浮面板，包含：
 *   - 亮度 / 对比度 / 饱和度 / 锐化 / 暗角 / Gamma SeekBar
 *   - LUT 强度 SeekBar
 *   - 开关 Toggle
 *   - 最小化按钮
 */
class OverlayService : Service() {

    companion object {
        const val CHANNEL_ID = "reshade_overlay"
        const val NOTIF_ID   = 1001
    }

    private lateinit var windowManager: WindowManager
    private lateinit var overlayView: View
    private var targetPackage: String = ""
    private var isMinimized = false

    // 参数值（默认）
    private var brightness  = 0.0f    // -100 ~ +100 → 除以 100
    private var contrast    = 100.0f  // 0 ~ 300
    private var saturation  = 100.0f  // 0 ~ 300
    private var sharpness   = 0.0f    // 0 ~ 500
    private var vignette    = 0.0f    // 0 ~ 100
    private var gamma       = 100.0f  // 10 ~ 300
    private var lutStrength = 100.0f  // 0 ~ 100
    private var enabled     = true

    override fun onCreate() {
        super.onCreate()
        windowManager = getSystemService(WINDOW_SERVICE) as WindowManager
        createNotificationChannel()
        startForeground(NOTIF_ID, buildNotification())
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        targetPackage = intent?.getStringExtra("package") ?: ""
        createOverlay()
        return START_STICKY
    }

    override fun onBind(intent: Intent?) = null

    // ─────────────────────────────────────────────────────
    // 创建悬浮窗
    // ─────────────────────────────────────────────────────
    private fun createOverlay() {
        overlayView = LayoutInflater.from(this)
            .inflate(R.layout.overlay_panel, null)

        val params = WindowManager.LayoutParams(
            WindowManager.LayoutParams.WRAP_CONTENT,
            WindowManager.LayoutParams.WRAP_CONTENT,
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
                WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
            else
                @Suppress("DEPRECATION")
                WindowManager.LayoutParams.TYPE_PHONE,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE,
            PixelFormat.TRANSLUCENT
        ).apply {
            gravity = Gravity.TOP or Gravity.START
            x = 20
            y = 200
        }

        setupDragToMove(overlayView, params)
        setupControls(overlayView)
        windowManager.addView(overlayView, params)
    }

    // ─────────────────────────────────────────────────────
    // 拖拽移动
    // ─────────────────────────────────────────────────────
    private fun setupDragToMove(view: View, params: WindowManager.LayoutParams) {
        val titleBar = view.findViewById<View>(R.id.titleBar)
        var initX = 0; var initY = 0
        var touchX = 0f; var touchY = 0f

        titleBar.setOnTouchListener { _, event ->
            when (event.action) {
                MotionEvent.ACTION_DOWN -> {
                    initX = params.x; initY = params.y
                    touchX = event.rawX; touchY = event.rawY
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    params.x = initX + (event.rawX - touchX).toInt()
                    params.y = initY + (event.rawY - touchY).toInt()
                    windowManager.updateViewLayout(view, params)
                    true
                }
                else -> false
            }
        }
    }

    // ─────────────────────────────────────────────────────
    // 绑定控件
    // ─────────────────────────────────────────────────────
    private fun setupControls(view: View) {
        val switchEnable  = view.findViewById<Switch>(R.id.switchEnable)
        val btnMinimize   = view.findViewById<Button>(R.id.btnMinimize)
        val panelContent  = view.findViewById<View>(R.id.panelContent)

        switchEnable.isChecked = enabled
        switchEnable.setOnCheckedChangeListener { _, checked ->
            enabled = checked
            sendToggle(checked)
        }

        btnMinimize.setOnClickListener {
            isMinimized = !isMinimized
            panelContent.visibility = if (isMinimized) View.GONE else View.VISIBLE
            btnMinimize.text = if (isMinimized) "▼" else "▲"
        }

        // SeekBar 绑定
        bindSeekBar(view, R.id.sbBrightness,  R.id.tvBrightnessVal,  200, 100) { v ->
            brightness = (v - 100).toFloat(); sendParams() }
        bindSeekBar(view, R.id.sbContrast,    R.id.tvContrastVal,    300, 100) { v ->
            contrast = v.toFloat(); sendParams() }
        bindSeekBar(view, R.id.sbSaturation,  R.id.tvSaturationVal,  300, 100) { v ->
            saturation = v.toFloat(); sendParams() }
        bindSeekBar(view, R.id.sbSharpness,   R.id.tvSharpnessVal,   500, 0) { v ->
            sharpness = v.toFloat(); sendParams() }
        bindSeekBar(view, R.id.sbVignette,    R.id.tvVignetteVal,    100, 0) { v ->
            vignette = v.toFloat(); sendParams() }
        bindSeekBar(view, R.id.sbGamma,       R.id.tvGammaVal,       300, 100) { v ->
            gamma = v.toFloat(); sendParams() }
        bindSeekBar(view, R.id.sbLUTStrength, R.id.tvLUTStrengthVal, 100, 100) { v ->
            lutStrength = v.toFloat(); sendParams() }

        // 重置按钮
        view.findViewById<Button>(R.id.btnReset).setOnClickListener {
            resetParams()
            rebindAll(view)
            sendParams()
        }
    }

    private fun bindSeekBar(
        root: View, sbId: Int, tvId: Int,
        max: Int, default: Int,
        onChange: (Int) -> Unit
    ) {
        val sb = root.findViewById<SeekBar>(sbId)
        val tv = root.findViewById<TextView>(tvId)
        sb.max      = max
        sb.progress = default
        tv.text     = default.toString()
        sb.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(s: SeekBar, v: Int, fromUser: Boolean) {
                tv.text = v.toString()
                onChange(v)
            }
            override fun onStartTrackingTouch(s: SeekBar) {}
            override fun onStopTrackingTouch(s: SeekBar) {}
        })
    }

    private fun rebindAll(view: View) {
        fun setProgress(id: Int, v: Int) = view.findViewById<SeekBar>(id).also { it.progress = v }
        setProgress(R.id.sbBrightness,  100 + brightness.toInt())
        setProgress(R.id.sbContrast,    contrast.toInt())
        setProgress(R.id.sbSaturation,  saturation.toInt())
        setProgress(R.id.sbSharpness,   sharpness.toInt())
        setProgress(R.id.sbVignette,    vignette.toInt())
        setProgress(R.id.sbGamma,       gamma.toInt())
        setProgress(R.id.sbLUTStrength, lutStrength.toInt())
    }

    private fun resetParams() {
        brightness = 0f; contrast = 100f; saturation = 100f
        sharpness  = 0f; vignette = 0f;   gamma = 100f; lutStrength = 100f
    }

    // ─────────────────────────────────────────────────────
    // IPC 发送
    // ─────────────────────────────────────────────────────
    private fun sendParams() {
        val p = IpcClient.Params(
            brightness  = brightness / 100f,
            contrast    = contrast   / 100f,
            saturation  = saturation / 100f,
            sharpness   = sharpness  / 100f,
            vignette    = vignette   / 100f,
            gamma       = gamma      / 100f,
            lutStrength = lutStrength / 100f
        )
        IpcClient.sendParams(targetPackage, p)
    }

    private fun sendToggle(on: Boolean) {
        IpcClient.sendToggle(targetPackage, on)
    }

    override fun onDestroy() {
        super.onDestroy()
        if (::overlayView.isInitialized)
            windowManager.removeView(overlayView)
    }

    // ─────────────────────────────────────────────────────
    // 前台通知
    // ─────────────────────────────────────────────────────
    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val ch = NotificationChannel(
                CHANNEL_ID, "ReShade Overlay",
                NotificationManager.IMPORTANCE_LOW
            )
            getSystemService(NotificationManager::class.java)
                .createNotificationChannel(ch)
        }
    }

    private fun buildNotification(): Notification {
        val stopIntent = PendingIntent.getService(
            this, 0,
            Intent(this, OverlayService::class.java).setAction("STOP"),
            PendingIntent.FLAG_IMMUTABLE
        )
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("AndroidReShade 运行中")
            .setContentText("目标：$targetPackage")
            .setSmallIcon(android.R.drawable.ic_menu_camera)
            .addAction(android.R.drawable.ic_delete, "停止", stopIntent)
            .setOngoing(true)
            .build()
    }
}
