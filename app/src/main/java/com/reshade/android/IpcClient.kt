package com.reshade.android

import android.util.Log
import java.io.DataOutputStream
import java.net.Socket
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.io.File

/**
 * IpcClient — 通过 UNIX domain socket 向注入库发送命令
 *
 * 协议与 reshade_core.cpp 中的 IpcMessage 对应（packed struct）：
 *   byte[0]   type: 0=params, 1=loadShader, 2=loadLUT, 3=toggle
 *   byte[1..] payload（按 type 变化）
 */
object IpcClient {

    private const val TAG = "ReShadeIPC"

    data class Params(
        val brightness:  Float = 0f,
        val contrast:    Float = 1f,
        val saturation:  Float = 1f,
        val sharpness:   Float = 0f,
        val vignette:    Float = 0f,
        val gamma:       Float = 1f,
        val lutStrength: Float = 1f
    )

    // ─────────────────────────────────────────────────────
    // 发送参数更新（type = 0）
    // ─────────────────────────────────────────────────────
    fun sendParams(packageName: String, p: Params) {
        val buf = ByteBuffer.allocate(1 + 7 * 4).order(ByteOrder.LITTLE_ENDIAN)
        buf.put(0.toByte())            // type = 0
        buf.putFloat(p.brightness)
        buf.putFloat(p.contrast)
        buf.putFloat(p.saturation)
        buf.putFloat(p.sharpness)
        buf.putFloat(p.vignette)
        buf.putFloat(p.gamma)
        buf.putFloat(p.lutStrength)
        sendRaw(packageName, buf.array())
    }

    // ─────────────────────────────────────────────────────
    // 发送加载 Shader 命令（type = 1）
    // ─────────────────────────────────────────────────────
    fun sendLoadShader(packageName: String, path: String) {
        sendFileCmd(packageName, 1, path)
    }

    // ─────────────────────────────────────────────────────
    // 发送加载 LUT 命令（type = 2）
    // ─────────────────────────────────────────────────────
    fun sendLoadLUT(packageName: String, path: String) {
        sendFileCmd(packageName, 2, path)
    }

    // ─────────────────────────────────────────────────────
    // 发送开关命令（type = 3）
    // ─────────────────────────────────────────────────────
    fun sendToggle(packageName: String, enabled: Boolean) {
        val buf = ByteBuffer.allocate(2).order(ByteOrder.LITTLE_ENDIAN)
        buf.put(3.toByte())
        buf.put(if (enabled) 1.toByte() else 0.toByte())
        sendRaw(packageName, buf.array())
    }

    // ─────────────────────────────────────────────────────
    // 私有辅助
    // ─────────────────────────────────────────────────────
    private fun sendFileCmd(packageName: String, type: Int, path: String) {
        val pathBytes = path.toByteArray(Charsets.UTF_8)
        val buf = ByteBuffer.allocate(1 + 256).order(ByteOrder.LITTLE_ENDIAN)
        buf.put(type.toByte())
        buf.put(pathBytes.copyOf(minOf(pathBytes.size, 255)))  // 最多 255 字节 + null
        sendRaw(packageName, buf.array())
    }

    private fun sendRaw(packageName: String, data: ByteArray) {
        val sockPath = "/data/local/tmp/reshade_${packageName}.sock"
        Thread {
            try {
                // Android 上 UNIX socket 用 LocalSocket
                val localSocket = android.net.LocalSocket()
                localSocket.connect(
                    android.net.LocalSocketAddress(
                        sockPath,
                        android.net.LocalSocketAddress.Namespace.FILESYSTEM
                    )
                )
                localSocket.outputStream.write(data)
                localSocket.outputStream.flush()
                localSocket.close()
                Log.d(TAG, "IPC sent ${data.size} bytes to $sockPath")
            } catch (e: Exception) {
                Log.w(TAG, "IPC send failed: ${e.message}")
            }
        }.start()
    }

    // ─────────────────────────────────────────────────────
    // 通过 root shell 写目标包名配置
    // ─────────────────────────────────────────────────────
    fun writeTargetConfig(packageName: String): Boolean {
        return try {
            val process = Runtime.getRuntime().exec("su")
            val out = DataOutputStream(process.outputStream)
            out.writeBytes("echo '$packageName' > /data/local/tmp/reshade_targets.txt\n")
            out.writeBytes("chmod 644 /data/local/tmp/reshade_targets.txt\n")
            out.writeBytes("exit\n")
            out.flush()
            process.waitFor() == 0
        } catch (e: Exception) {
            Log.e(TAG, "writeTargetConfig failed: ${e.message}")
            false
        }
    }

    // ─────────────────────────────────────────────────────
    // 推送核心库到 /data/local/tmp/
    // ─────────────────────────────────────────────────────
    fun deployCoreLib(apkLibPath: String): Boolean {
        return try {
            val process = Runtime.getRuntime().exec("su")
            val out = DataOutputStream(process.outputStream)
            out.writeBytes("cp '$apkLibPath' /data/local/tmp/libreshade_core.so\n")
            out.writeBytes("chmod 755 /data/local/tmp/libreshade_core.so\n")
            out.writeBytes("exit\n")
            out.flush()
            process.waitFor() == 0
        } catch (e: Exception) {
            Log.e(TAG, "deployCoreLib failed: ${e.message}")
            false
        }
    }
}
