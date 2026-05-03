#!/system/bin/sh
# service.sh - 在设备启动后执行（每次开机）
# 负责：Vulkan Layer 软链接、日志清理、版本检查

MODDIR=${0%/*}
TAG="AndroidReShade"

log_info() { log -t "$TAG" "$1"; }

log_info "AndroidReShade service starting..."

# ============================================================
# 等待系统完全启动
# ============================================================
until [ "$(getprop sys.boot_completed)" = "1" ]; do
    sleep 1
done
sleep 3

# ============================================================
# 更新 Vulkan Layer 软链接（Android 10+）
# ============================================================
API_LEVEL=$(getprop ro.build.version.sdk)
if [ "$API_LEVEL" -ge 29 ]; then
    LAYER_SRC="$MODDIR/system/lib64/libVkLayerReShade.so"
    LAYER_DST="/data/local/debug/vulkan/libVkLayerReShade.so"
    if [ -f "$LAYER_SRC" ]; then
        mkdir -p /data/local/debug/vulkan
        ln -sf "$LAYER_SRC" "$LAYER_DST" 2>/dev/null
        chmod 644 "$LAYER_DST" 2>/dev/null
        log_info "Vulkan Layer linked: $LAYER_DST"
    fi
fi

# ============================================================
# 清理过期日志（保留最近 5 个）
# ============================================================
LOG_DIR="/sdcard/ReShade/logs"
if [ -d "$LOG_DIR" ]; then
    ls -t "$LOG_DIR"/*.log 2>/dev/null | tail -n +6 | xargs rm -f 2>/dev/null
fi

log_info "AndroidReShade service started."
