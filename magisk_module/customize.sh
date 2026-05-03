#!/system/bin/sh
# AndroidReShade Magisk Module - customize.sh
# 在 Magisk 安装时执行

SKIPUNZIP=1

# ============================================================
# 工具函数
# ============================================================
print_info() { ui_print "[AndroidReShade] $1"; }
print_err()  { ui_print "[AndroidReShade] ERROR: $1"; abort; }

# ============================================================
# 检查运行环境
# ============================================================
print_info "=================================="
print_info " AndroidReShade Installer v1.0.0"
print_info "=================================="
print_info ""

# 检查 Android 版本（需要 API 28+）
API_LEVEL=$(getprop ro.build.version.sdk)
if [ "$API_LEVEL" -lt 28 ]; then
    print_err "Requires Android 9 (API 28+), current: API $API_LEVEL"
fi
print_info "Android API Level: $API_LEVEL  [OK]"

# 检查 ABI（仅支持 arm64-v8a）
ABI=$(getprop ro.product.cpu.abi)
if [ "$ABI" != "arm64-v8a" ]; then
    print_info "WARNING: Detected ABI=$ABI, only arm64-v8a is officially supported"
fi
print_info "Device ABI: $ABI"

# 检查 Zygisk 是否启用
ZYGISK=$(getprop persist.magisk.zygisk)
if [ "$ZYGISK" != "true" ] && [ "$ZYGISK" != "1" ]; then
    print_info "WARNING: Zygisk may not be enabled. Enable it in Magisk settings."
else
    print_info "Zygisk: enabled  [OK]"
fi

# ============================================================
# 解压模块文件
# ============================================================
print_info "Extracting module files..."
unzip -o "$ZIPFILE" 'system/*'  -d "$MODPATH" >&2
unzip -o "$ZIPFILE" 'zygisk/*'  -d "$MODPATH" >&2

# ============================================================
# 创建 ReShade 配置目录
# ============================================================
print_info "Creating ReShade directories..."
mkdir -p /sdcard/ReShade/shaders
mkdir -p /sdcard/ReShade/luts
mkdir -p /sdcard/ReShade/presets
mkdir -p /sdcard/ReShade/logs

# 复制示例 shader 和 LUT
ASSET_DIR="$MODPATH/system/sdcard/ReShade"
if [ -d "$ASSET_DIR/shaders" ]; then
    cp -r "$ASSET_DIR/shaders/"* /sdcard/ReShade/shaders/ 2>/dev/null
    print_info "Example shaders installed to /sdcard/ReShade/shaders/"
fi
if [ -d "$ASSET_DIR/luts" ]; then
    cp -r "$ASSET_DIR/luts/"* /sdcard/ReShade/luts/ 2>/dev/null
    print_info "Example LUTs installed to /sdcard/ReShade/luts/"
fi

# 写入默认配置
cat > /sdcard/ReShade/reshade.ini << 'EOF'
[General]
; AndroidReShade 配置文件
; 编辑此文件以自定义行为

[Targets]
; 目标游戏包名（每行一个）
; 示例：
; PackageName=com.tencent.tmgp.pubgmhd
; PackageName=com.netease.x19
; PackageName=*  ; 匹配所有应用（不推荐）

[Effects]
; 初始启用的效果（逗号分隔）
; Enabled=ColorCorrection,Sharpen

[Performance]
; 最大帧率限制（-1=不限制）
MaxFPS=-1
; 是否启用 GPU 计时
EnableTimer=false

[Debug]
; 输出调试日志到 /sdcard/ReShade/logs/
LogLevel=info
EOF

print_info "Default config written to /sdcard/ReShade/reshade.ini"

# ============================================================
# 设置 Vulkan Layer（Android 10+）
# ============================================================
if [ "$API_LEVEL" -ge 29 ]; then
    print_info "Setting up Vulkan Layer..."
    VULKAN_LAYER_DIR="/data/local/debug/vulkan"
    mkdir -p "$VULKAN_LAYER_DIR"
    # Vulkan layer 将在 APK 安装后自动链接
    print_info "Vulkan Layer directory: $VULKAN_LAYER_DIR"
fi

# ============================================================
# 权限设置
# ============================================================
set_perm_recursive "$MODPATH" root root 0755 0644
set_perm_recursive /sdcard/ReShade shell sdcard_rw 0775 0664

# ============================================================
# 完成
# ============================================================
print_info ""
print_info "=================================="
print_info " Installation Complete!"
print_info ""
print_info " Next steps:"
print_info " 1. Reboot your device"
print_info " 2. Install AndroidReShade.apk"
print_info " 3. Open the app and add target game"
print_info " 4. Put shaders in /sdcard/ReShade/shaders/"
print_info " 5. Enjoy!"
print_info "=================================="
