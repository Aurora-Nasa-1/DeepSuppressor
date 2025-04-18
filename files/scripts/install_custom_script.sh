#!/system/bin/sh

# Custom Script
# -----------------
# This script extends the functionality of the default and setup scripts, allowing direct use of their variables and functions.
# SCRIPT_EN.md
if [ "$ARCH" = "arm64" ]; then
    rm -f "$MODPATH/bin/process_manager-x86_64"
    mv "$MODPATH/bin/process_manager-aarch64" "$MODPATH/bin/process_manager"
    log_info "Architecture: arm64, using aarch64 binary"
fi
if [ "$ARCH" = "x64" ]; then
    rm -f "$MODPATH/bin/process_manager-aarch64"
    mv "$MODPATH/bin/process_manager-x86_64" "$MODPATH/bin/process_manager"
    log_info "Architecture: x64, using x86_64 binary"
fi
