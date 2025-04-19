#!/system/bin/sh
# 高性能日志系统 - 与C++日志组件集成
# 版本: 2.2.0

# ============================
# 全局变量
# ============================
LOGGER_VERSION="2.2.0"
LOGGER_INITIALIZED=0
LOG_FILE_NAME="system"
LOGMONITOR_PID=""
LOGMONITOR_BIN="${MODPATH}/bin/logmonitor"
LOG_LEVEL=3  # 1=ERROR, 2=WARN, 3=INFO, 4=DEBUG
LOW_POWER_MODE=0  # 默认关闭低功耗模式

# ============================
# 核心功能
# ============================

# 初始化日志系统
init_logger() {
    [ "$LOGGER_INITIALIZED" = "1" ] && return 0
    
    # 日志目录
    LOG_DIR="${MODPATH}/logs"
    mkdir -p "$LOG_DIR" 2>/dev/null
    
    # 启动logmonitor守护进程
    if [ -f "$LOGMONITOR_BIN" ]; then
        LOGMONITOR_PID=$(pgrep -f "$LOGMONITOR_BIN.*daemon" 2>/dev/null)
        
        if [ -z "$LOGMONITOR_PID" ]; then
            # 添加低功耗模式参数
            if [ "$LOW_POWER_MODE" = "1" ]; then
                "$LOGMONITOR_BIN" -c daemon -d "$LOG_DIR" -l "$LOG_LEVEL" -p >/dev/null 2>&1 &
            else
                "$LOGMONITOR_BIN" -c daemon -d "$LOG_DIR" -l "$LOG_LEVEL" >/dev/null 2>&1 &
            fi
            LOGMONITOR_PID=$!
            sleep 0.1  # 减少等待时间
        fi
    else
        Aurora_ui_print "$FILE_NOT_FOUND logmonitor" >&2
        return 1
    fi
    
    LOGGER_INITIALIZED=1
    return 0
}

# 日志函数
log() {
    local level="$1"
    local message="$2"
    
    [ "$level" -gt "$LOG_LEVEL" ] && return 0
    [ "$LOGGER_INITIALIZED" != "1" ] && init_logger
    
    # 添加低功耗模式参数
    if [ "$LOW_POWER_MODE" = "1" ]; then
        "$LOGMONITOR_BIN" -c write -n "$LOG_FILE_NAME" -m "$message" -l "$level" -p
    else
        "$LOGMONITOR_BIN" -c write -n "$LOG_FILE_NAME" -m "$message" -l "$level"
    fi
}

log_error() { log 1 "$1"; }
log_warn()  { log 2 "$1"; }
log_info()  { log 3 "$1"; }
log_debug() { log 4 "$1"; }

# 批量日志写入
batch_log() {
    local batch_file="$1"
    
    [ ! -f "$batch_file" ] && return 1
    [ "$LOGGER_INITIALIZED" != "1" ] && init_logger
    
    # 添加低功耗模式参数
    if [ "$LOW_POWER_MODE" = "1" ]; then
        "$LOGMONITOR_BIN" -c batch -n "$LOG_FILE_NAME" -b "$batch_file" -p
    else
        "$LOGMONITOR_BIN" -c batch -n "$LOG_FILE_NAME" -b "$batch_file"
    fi
    
    return $?
}

# 设置日志文件
set_log_file() {
    [ -z "$1" ] && return 1
    LOG_FILE_NAME="$1"
    return 0
}

# 设置日志级别
set_log_level() {
    [ -z "$1" ] && return 1
    LOG_LEVEL="$1"
    return 0
}

# 启用/禁用低功耗模式
set_low_power_mode() {
    if [ "$1" = "1" ] || [ "$1" = "true" ] || [ "$1" = "on" ]; then
        LOW_POWER_MODE=1
    else
        LOW_POWER_MODE=0
    fi
    
    # 如果日志系统已初始化，则需要重启
    if [ "$LOGGER_INITIALIZED" = "1" ]; then
        stop_logger
        init_logger
    fi
    
    return 0
}

# 刷新日志
flush_logs() {
    [ "$LOGGER_INITIALIZED" = "1" ] && "$LOGMONITOR_BIN" -c flush
}

# 清理日志
clean_logs() {
    [ "$LOGGER_INITIALIZED" = "1" ] && "$LOGMONITOR_BIN" -c clean
}

# 停止日志系统
stop_logger() {
    if [ "$LOGGER_INITIALIZED" = "1" ] && [ -n "$LOGMONITOR_PID" ]; then
        # 先刷新缓冲区
        "$LOGMONITOR_BIN" -c flush
        # 等待刷新完成
        sleep 0.5
        # 发送终止信号
        kill -TERM "$LOGMONITOR_PID" 2>/dev/null
        # 等待进程退出
        wait "$LOGMONITOR_PID" 2>/dev/null
        LOGGER_INITIALIZED=0
    fi
}

