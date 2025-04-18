#!/system/bin/sh
MODDIR=${0%/*}
MODPATH="$MODDIR"

# 初始化日志目录
LOG_DIR="$MODPATH/logs"
mkdir -p "$LOG_DIR"

# 启动日志监控器（如果存在）
if [ -f "$MODPATH/bin/logmonitor" ]; then
    # 检查是否已经启动
    if ! pgrep -f "$MODPATH/bin/logmonitor.*-c daemon" >/dev/null; then
        "$MODPATH/bin/logmonitor" -c start -d "$LOG_DIR" >/dev/null 2>&1 &
    fi
    # 设置日志文件名为action
    "$MODPATH/bin/logmonitor" -c write -n "action" -m "action.sh 被调用" -l 3 >/dev/null 2>&1
fi

if [ ! -f "$MODPATH/files/scripts/default_scripts/main.sh" ]; then
    log_error "File not found: $MODPATH/files/scripts/default_scripts/main.sh"
    exit 1
else
    . "$MODPATH/files/scripts/default_scripts/main.sh"
    # 记录action.sh被调用
    start_script
    log_info "action.sh was called with parameters: $*"
fi
# 在这里添加您的自定义脚本逻辑
# -----------------
# This script extends the functionality of the default and setup scripts, allowing direct use of their variables and functions.
# SCRIPT_EN.md

# 定义清理进程函数
stop_logger