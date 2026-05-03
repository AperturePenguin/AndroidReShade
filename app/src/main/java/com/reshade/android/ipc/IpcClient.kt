package com.reshade.android.ipc

import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.util.Log
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * IPC 消息类型（与 reshade_core.cpp 的 IpcMessage 结构体对齐）
 */
sealed class IpcMessage {
    abstract fun encode(): ByteArray

    /** type=0: 设置单个 float uniform */
    data class SetUniform(
        val effectId: Int,
        val name:     String,
        val value:    Float
    ) : IpcMessage() {
        override fun encode(): ByteArray {
            val nameBytes = name.toByteArray(Charsets.UTF_8).copyOf(64)
            val buf = ByteBuffer.allocate(1 + 4 + 64 + 4).order(ByteOrder.LITTLE_ENDIAN)
            buf.put(0.toByte())
            buf.putInt(effectId)
            buf.put(nameBytes)
            buf.putFloat(value)
            return buf.array()
        }
    }

    /** type=1: 批量参数更新 */
    data class BulkParams(
        val effectId: Int,
        val params:   Map<String, Float>
    ) : IpcMessage() {
        override fun encode(): ByteArray {
            val buf = ByteBuffer.allocate(1 + 4 + 4 + params.size * (64 + 4))
                .order(ByteOrder.LITTLE_ENDIAN)
            buf.put(1.toByte())
            buf.putInt(effectId)
            buf.putInt(params.size)
            for ((k, v) in params) {
                buf.put(k.toByteArray(Charsets.UTF_8).copyOf(64))
                buf.putFloat(v)
            }
            return buf.array()
        }
    }

    /** type=2: 开关 Effect */
    data class ToggleEffect(val effectId: Int, val enabled: Boolean) : IpcMessage() {
        override fun encode(): ByteArray {
            val buf = ByteBuffer.allocate(6).order(ByteOrder.LITTLE_ENDIAN)
            buf.put(2.toByte())
            buf.putInt(effectId)
            buf.put(if (enabled) 1.toByte() else 0.toByte())
            return buf.array()
        }
    }

    /** type=3: 加载 shader 文件 */
    data class LoadShader(val path: String) : IpcMessage() {
        override fun encode(): ByteArray {
            val buf = ByteBuffer.allocate(1 + 256).order(ByteOrder.LITTLE_ENDIAN)
            buf.put(3.toByte())
            buf.put(path.toByteArray(Charsets.UTF_8).copyOf(255))
            return buf.array()
        }
    }

    /** type=4: 加载 LUT 文件 */
    data class LoadLut(val path: String, val strength: Float = 1f) : IpcMessage() {
        override fun encode(): ByteArray {
            val buf = ByteBuffer.allocate(1 + 256 + 4).order(ByteOrder.LITTLE_ENDIAN)
            buf.put(4.toByte())
            buf.put(path.toByteArray(Charsets.UTF_8).copyOf(255))
            buf.putFloat(strength)
            return buf.array()
        }
    }

    /** type=5: 全局开关 */
    data class GlobalToggle(val enabled: Boolean) : IpcMessage() {
        override fun encode() = byteArrayOf(5, if (enabled) 1 else 0)
    }

    companion object {
        fun setUniform(effectId: Int, name: String, value: Float) =
            SetUniform(effectId, name, value)
        fun toggleEffect(effectId: Int, enabled: Boolean) =
            ToggleEffect(effectId, enabled)
    }
}

/**
 * IPC 客户端：通过 UNIX domain socket 向注入库发送命令
 */
class IpcClient(private val packageName: String) {

    companion object {
        private const val TAG = "RS::IpcClient"
    }

    fun send(msg: IpcMessage) {
        val data = msg.encode()
        Thread {
            try {
                val sock = LocalSocket()
                sock.connect(LocalSocketAddress(
                    "/data/local/tmp/reshade_${packageName}.sock",
                    LocalSocketAddress.Namespace.FILESYSTEM
                ))
                sock.outputStream.write(data)
                sock.outputStream.flush()
                sock.close()
            } catch (e: Exception) {
                Log.w(TAG, "send failed: ${e.message}")
            }
        }.start()
    }

    /** 发送批量参数（防抖：100ms 内合并）*/
    private val pendingParams = mutableMapOf<Int, MutableMap<String, Float>>()
    private var debounceJob: Thread? = null

    fun sendParamDebounced(effectId: Int, name: String, value: Float) {
        synchronized(pendingParams) {
            pendingParams.getOrPut(effectId) { mutableMapOf() }[name] = value
        }
        debounceJob?.interrupt()
        debounceJob = Thread {
            try {
                Thread.sleep(50)
                val snapshot: Map<Int, Map<String, Float>>
                synchronized(pendingParams) {
                    snapshot = pendingParams.toMap()
                    pendingParams.clear()
                }
                for ((id, params) in snapshot)
                    send(IpcMessage.BulkParams(id, params))
            } catch (_: InterruptedException) {}
        }.also { it.start() }
    }
}
