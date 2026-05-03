#!/system/bin/sh
# uninstall.sh - Magisk 卸载时执行

ui_print "[AndroidReShade] Uninstalling..."

# 删除 Vulkan Layer
rm -f /data/local/debug/vulkan/libVkLayerReShade.so

# 不删除用户数据（shader、LUT、preset）
# 用户可手动删除 /sdcard/ReShade/

ui_print "[AndroidReShade] Uninstall complete."
ui_print "[AndroidReShade] User data retained at /sdcard/ReShade/"
