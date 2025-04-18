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
cleanup_processes() {
    log_info "开始清理进程"
    
    # 关闭logmonitor进程
    if [ -f "$MODPATH/bin/logmonitor" ]; then
        log_info "正在停止logmonitor服务"
        "$MODPATH/bin/logmonitor" -c stop >/dev/null 2>&1
        
        # 查找并终止所有logmonitor进程
        for pid in $(ps -ef | grep "[l]ogmonitor" | awk '{print $2}'); do
            log_debug "正在终止进程 logmonitor (PID: $pid)"
            kill -9 "$pid" >/dev/null 2>&1
        done
    fi
    
    # 确保日志被刷新
    flush_log
    
    log_info "进程清理完成"
}

# 确保日志被刷新
if [ -f "$MODPATH/bin/logmonitor" ]; then
    "$MODPATH/bin/logmonitor" -c flush >/dev/null 2>&1
else
    flush_log
fi

# 脚本结束前清理进程
cleanup_processes