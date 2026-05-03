package com.reshade.android

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.Settings
import android.widget.*
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import java.io.File

class MainActivity : AppCompatActivity() {

    companion object {
        const val REQ_OVERLAY_PERMISSION = 1001
        const val REQ_PICK_SHADER = 1002
        const val REQ_PICK_LUT = 1003
        const val RESHADE_DIR = "/sdcard/ReShade"
    }

    private lateinit var targetPackageEdit: EditText
    private lateinit var shaderListView: RecyclerView
    private lateinit var lutListView: RecyclerView
    private lateinit var btnStartOverlay: Button
    private lateinit var btnPickShader: Button
    private lateinit var btnPickLUT: Button
    private lateinit var statusText: TextView

    private val shaderAdapter = FileListAdapter(mutableListOf()) { path -> activateShader(path) }
    private val lutAdapter    = FileListAdapter(mutableListOf()) { path -> activateLUT(path) }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        // 确保 ReShade 目录存在
        File(RESHADE_DIR).mkdirs()
        File("$RESHADE_DIR/shaders").mkdirs()
        File("$RESHADE_DIR/luts").mkdirs()

        initViews()
        scanFiles()
        checkPermissions()
    }

    private fun initViews() {
        targetPackageEdit = findViewById(R.id.etTargetPackage)
        btnStartOverlay   = findViewById(R.id.btnStartOverlay)
        btnPickShader     = findViewById(R.id.btnPickShader)
        btnPickLUT        = findViewById(R.id.btnPickLUT)
        statusText        = findViewById(R.id.tvStatus)
        shaderListView    = findViewById(R.id.rvShaders)
        lutListView       = findViewById(R.id.rvLuts)

        shaderListView.layoutManager = LinearLayoutManager(this)
        shaderListView.adapter = shaderAdapter
        lutListView.layoutManager = LinearLayoutManager(this)
        lutListView.adapter = lutAdapter

        btnStartOverlay.setOnClickListener {
            val pkg = targetPackageEdit.text.toString().trim()
            if (pkg.isEmpty()) {
                showToast("请输入目标包名")
                return@setOnClickListener
            }
            saveTargetPackage(pkg)
            startOverlayService(pkg)
        }

        btnPickShader.setOnClickListener {
            val intent = Intent(Intent.ACTION_GET_CONTENT).apply {
                type = "*/*"
                putExtra(Intent.EXTRA_MIME_TYPES, arrayOf("*/*"))
            }
            startActivityForResult(intent, REQ_PICK_SHADER)
        }

        btnPickLUT.setOnClickListener {
            val intent = Intent(Intent.ACTION_GET_CONTENT).apply {
                type = "*/*"
            }
            startActivityForResult(intent, REQ_PICK_LUT)
        }
    }

    private fun checkPermissions() {
        if (!Settings.canDrawOverlays(this)) {
            AlertDialog.Builder(this)
                .setTitle("需要悬浮窗权限")
                .setMessage("AndroidReShade 需要悬浮窗权限来显示参数调节面板。")
                .setPositiveButton("去开启") { _, _ ->
                    val intent = Intent(
                        Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                        Uri.parse("package:$packageName")
                    )
                    startActivityForResult(intent, REQ_OVERLAY_PERMISSION)
                }
                .show()
        }
    }

    private fun scanFiles() {
        val shaderDir = File("$RESHADE_DIR/shaders")
        val lutDir    = File("$RESHADE_DIR/luts")

        val shaders = shaderDir.listFiles()
            ?.filter { it.extension in listOf("fx", "hlsl") }
            ?.map { it.absolutePath }
            ?.toMutableList() ?: mutableListOf()

        val luts = lutDir.listFiles()
            ?.filter { it.extension in listOf("cube", "png") }
            ?.map { it.absolutePath }
            ?.toMutableList() ?: mutableListOf()

        shaderAdapter.updateData(shaders)
        lutAdapter.updateData(luts)
        statusText.text = "已扫描：${shaders.size} 个 Shader，${luts.size} 个 LUT"
    }

    private fun activateShader(path: String) {
        val pkg = targetPackageEdit.text.toString().trim()
        if (pkg.isEmpty()) { showToast("请先填写目标包名"); return }
        IpcClient.sendLoadShader(pkg, path)
        showToast("已发送 Shader：${File(path).name}")
    }

    private fun activateLUT(path: String) {
        val pkg = targetPackageEdit.text.toString().trim()
        if (pkg.isEmpty()) { showToast("请先填写目标包名"); return }
        IpcClient.sendLoadLUT(pkg, path)
        showToast("已发送 LUT：${File(path).name}")
    }

    private fun saveTargetPackage(pkg: String) {
        val file = File("/data/local/tmp/reshade_targets.txt")
        file.writeText("# AndroidReShade 目标包名\n$pkg\n")
        showToast("目标已保存：$pkg")
    }

    private fun startOverlayService(pkg: String) {
        val intent = Intent(this, OverlayService::class.java).apply {
            putExtra("package", pkg)
        }
        startForegroundService(intent)
        showToast("悬浮窗已启动")
    }

    private fun showToast(msg: String) {
        Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (resultCode != RESULT_OK || data?.data == null) return

        when (requestCode) {
            REQ_PICK_SHADER -> copyFileTo(data.data!!, "$RESHADE_DIR/shaders")
            REQ_PICK_LUT    -> copyFileTo(data.data!!, "$RESHADE_DIR/luts")
        }
        scanFiles()
    }

    private fun copyFileTo(uri: Uri, destDir: String) {
        val fileName = getFileName(uri) ?: "unknown"
        val dest = File("$destDir/$fileName")
        contentResolver.openInputStream(uri)?.use { input ->
            dest.outputStream().use { out -> input.copyTo(out) }
        }
        showToast("已导入：$fileName")
    }

    private fun getFileName(uri: Uri): String? {
        contentResolver.query(uri, null, null, null, null)?.use { cursor ->
            val idx = cursor.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
            if (cursor.moveToFirst() && idx >= 0) return cursor.getString(idx)
        }
        return uri.lastPathSegment
    }
}

// ─────────────────────────────────────────────────────────
// RecyclerView Adapter
// ─────────────────────────────────────────────────────────
class FileListAdapter(
    private val items: MutableList<String>,
    private val onItemClick: (String) -> Unit
) : RecyclerView.Adapter<FileListAdapter.VH>() {

    inner class VH(view: android.view.View) : RecyclerView.ViewHolder(view) {
        val name: TextView = view.findViewById(android.R.id.text1)
        val apply: Button  = view.findViewById(android.R.id.button1)
    }

    override fun onCreateViewHolder(parent: android.view.ViewGroup, viewType: Int): VH {
        val inflater = android.view.LayoutInflater.from(parent.context)
        val view = inflater.inflate(R.layout.item_file, parent, false)
        return VH(view)
    }

    override fun onBindViewHolder(holder: VH, position: Int) {
        val path = items[position]
        holder.name.text = java.io.File(path).name
        holder.apply.setOnClickListener { onItemClick(path) }
    }

    override fun getItemCount() = items.size

    fun updateData(newItems: MutableList<String>) {
        items.clear()
        items.addAll(newItems)
        notifyDataSetChanged()
    }
}
