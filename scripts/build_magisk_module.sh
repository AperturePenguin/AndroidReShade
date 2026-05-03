#!/usr/bin/env bash
# build_magisk_module.sh
# 打包 AndroidReShade Magisk 模块
#
# 用法：
#   bash scripts/build_magisk_module.sh [release|debug]
#
# 输出：
#   dist/AndroidReShade_v1.0.0_magisk.zip

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
DIST_DIR="$ROOT_DIR/dist"
VERSION="v1.0.0"
MODULE_NAME="AndroidReShade_${VERSION}_magisk"
BUILD_TYPE="${1:-release}"

echo "======================================"
echo " AndroidReShade Magisk Module Builder"
echo " Version: $VERSION  Build: $BUILD_TYPE"
echo "======================================"

mkdir -p "$DIST_DIR"
STAGING="$DIST_DIR/staging"
rm -rf "$STAGING"
mkdir -p "$STAGING"

# ============================================================
# 1. 复制 Magisk 模块框架文件
# ============================================================
echo "[1/5] Copying module framework files..."
cp "$ROOT_DIR/magisk_module/module.prop"     "$STAGING/"
cp "$ROOT_DIR/magisk_module/customize.sh"    "$STAGING/"
cp "$ROOT_DIR/magisk_module/service.sh"      "$STAGING/"
cp "$ROOT_DIR/magisk_module/uninstall.sh"    "$STAGING/"

# META-INF（Magisk 要求）
mkdir -p "$STAGING/META-INF/com/google/android"
cat > "$STAGING/META-INF/com/google/android/update-binary" << 'HEREDOC'
#!/sbin/sh
SKIPUNZIP=1
. /data/adb/magisk/util_functions.sh
. "$ZIPFILE" customize.sh
HEREDOC

cat > "$STAGING/META-INF/com/google/android/updater-script" << 'HEREDOC'
#MAGISK
HEREDOC

# ============================================================
# 2. 复制 Zygisk 注入模块 .so
# ============================================================
echo "[2/5] Copying Zygisk module..."
mkdir -p "$STAGING/zygisk"

# arm64-v8a（主要目标平台）
if [ -f "$ROOT_DIR/app/build/intermediates/cmake/$BUILD_TYPE/obj/arm64-v8a/libzygisk_reshade.so" ]; then
    cp "$ROOT_DIR/app/build/intermediates/cmake/$BUILD_TYPE/obj/arm64-v8a/libzygisk_reshade.so" \
       "$STAGING/zygisk/arm64-v8a.so"
    echo "  Zygisk arm64-v8a: OK"
else
    echo "  WARNING: libzygisk_reshade.so not found (arm64-v8a)"
    echo "           Run ./gradlew assembleRelease first, then repack"
    # 创建占位文件（仅用于包结构验证）
    touch "$STAGING/zygisk/arm64-v8a.so.placeholder"
fi

# armeabi-v7a（可选兼容）
if [ -f "$ROOT_DIR/app/build/intermediates/cmake/$BUILD_TYPE/obj/armeabi-v7a/libzygisk_reshade.so" ]; then
    cp "$ROOT_DIR/app/build/intermediates/cmake/$BUILD_TYPE/obj/armeabi-v7a/libzygisk_reshade.so" \
       "$STAGING/zygisk/armeabi-v7a.so"
    echo "  Zygisk armeabi-v7a: OK"
fi

# ============================================================
# 3. 复制核心注入库
# ============================================================
echo "[3/5] Copying core injection library..."
mkdir -p "$STAGING/system/lib64"

if [ -f "$ROOT_DIR/app/build/intermediates/cmake/$BUILD_TYPE/obj/arm64-v8a/libreshade_core.so" ]; then
    cp "$ROOT_DIR/app/build/intermediates/cmake/$BUILD_TYPE/obj/arm64-v8a/libreshade_core.so" \
       "$STAGING/system/lib64/"
    echo "  libreshade_core.so: OK"
else
    echo "  WARNING: libreshade_core.so not found"
    touch "$STAGING/system/lib64/libreshade_core.so.placeholder"
fi

# Vulkan Layer
if [ -f "$ROOT_DIR/app/build/intermediates/cmake/$BUILD_TYPE/obj/arm64-v8a/libVkLayerReShade.so" ]; then
    cp "$ROOT_DIR/app/build/intermediates/cmake/$BUILD_TYPE/obj/arm64-v8a/libVkLayerReShade.so" \
       "$STAGING/system/lib64/"
    echo "  libVkLayerReShade.so: OK"
fi

# ============================================================
# 4. 复制示例 shader 和 LUT
# ============================================================
echo "[4/5] Copying example shaders and LUTs..."
mkdir -p "$STAGING/system/sdcard/ReShade/shaders"
mkdir -p "$STAGING/system/sdcard/ReShade/luts"

cp "$ROOT_DIR/assets/shaders/"*.fx  "$STAGING/system/sdcard/ReShade/shaders/" 2>/dev/null || true
cp "$ROOT_DIR/assets/luts/"*.cube   "$STAGING/system/sdcard/ReShade/luts/"    2>/dev/null || true
echo "  Shaders: $(ls "$STAGING/system/sdcard/ReShade/shaders/" | wc -l) files"
echo "  LUTs:    $(ls "$STAGING/system/sdcard/ReShade/luts/"    | wc -l) files"

# ============================================================
# 5. 打包为 ZIP
# ============================================================
echo "[5/5] Creating ZIP package..."
OUTPUT="$DIST_DIR/${MODULE_NAME}.zip"
cd "$STAGING"
zip -r "$OUTPUT" . -x "*.placeholder" >/dev/null
cd "$ROOT_DIR"

echo ""
echo "======================================"
echo " Build Complete!"
echo " Output: $OUTPUT"
echo " Size:   $(du -sh "$OUTPUT" | cut -f1)"
echo ""
echo " Install:"
echo "   1. Copy to phone: adb push $OUTPUT /sdcard/"
echo "   2. In Magisk Manager: Modules → Install from storage"
echo "   3. Select the ZIP and reboot"
echo "======================================"
