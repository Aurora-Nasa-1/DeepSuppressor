#!/system/bin/sh

# Custom Script
# -----------------
# This script extends the functionality of the default and setup scripts, allowing direct use of their variables and functions.
# SCRIPT_EN.md
if [ "$ARCH" = "arm64" ]; then
    rm -f "$MODPATH/bin/process_manager-DeepSuppressor-x86_64"
    mv "$MODPATH/bin/process_manager-DeepSuppressor-aarch64" "$MODPATH/bin/process_manager"
    log_info "Architecture: arm64, using aarch64 binary"
fi
if [ "$ARCH" = "x64" ]; then
    rm -f "$MODPATH/bin/process_manager-DeepSuppressor-aarch64"
    mv "$MODPATH/bin/process_manager-DeepSuppressor-x86_64" "$MODPATH/bin/process_manager"
    log_info "Architecture: x64, using x86_64 binary"
fi

if [ "$ARCH" = "arm64" ]; then
    rm -f "$MODPATH/bin/process_manager_NOAI-DeepSuppressor-x86_64"
    mv "$MODPATH/bin/process_manager_NOAI-DeepSuppressor-aarch64" "$MODPATH/bin/process_manager_NOAI"
fi
if [ "$ARCH" = "x64" ]; then
    rm -f "$MODPATH/bin/process_manager_NOAI-DeepSuppressor-aarch64"
    mv "$MODPATH/bin/process_manager_NOAI-DeepSuppressor-x86_64" "$MODPATH/bin/process_manager_NOAI"
fi
ui_print ""
Aurora_ui_print "智能检测用户习惯动态调整功能"
Aurora_ui_print "可能存在耗电高，稳定性差等问题，但正常使用72小时之后使用体验可能会更佳"
ui_print ""
ui_print "- 按下音量上键关闭智能检测"
ui_print "- 按下音量下键开启智能检测"
key_select
if [ "$key_pressed" = "KEY_VOLUMEUP" ]; then
    rm -f "$MODPATH/bin/process_manager"
    mv "$MODPATH/bin/process_manager_NOAI" "$MODPATH/bin/process_manager"
fi
