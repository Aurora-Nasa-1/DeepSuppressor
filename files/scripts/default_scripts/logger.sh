#!/system/bin/sh
# 高性能日志系统 - 与C++日志组件集成
# 版本: 2.1.0

# ============================
# 全局变量
# ============================
LOGGER_VERSION="2.1.0"
LOGGER_INITIALIZED=0
LOG_FILE_NAME="system"
LOGMONITOR_PID=""
LOGMONITOR_BIN="${MODPATH}/bin/logmonitor"
LOG_LEVEL=3  # 1=ERROR, 2=WARN, 3=INFO, 4=DEBUG

# ============================
# 核心功能
# ============================

# 初始化日志系统
init_logger() {
    [ "$LOGGER_INITIALIZED" = "1" ] && return 0
    
    # 日志目录
    LOG_DIR="${MODPATH}/logs"
    mkdir -p "$LOG_DIR" 2>/dev/null || {
        echo "错误: 无法创建日志目录: $LOG_DIR" >&2
        return 1
    }
    
    # 启动logmonitor守护进程
    if [ -f "$LOGMONITOR_BIN" ]; then
        LOGMONITOR_PID=$(pgrep -f "$LOGMONITOR_BIN.*daemon" 2>/dev/null)
        
        if [ -z "$LOGMONITOR_PID" ]; then
            "$LOGMONITOR_BIN" -c daemon -d "$LOG_DIR" -l "$LOG_LEVEL" >/dev/null 2>&1 &
            LOGMONITOR_PID=$!
            sleep 0.1  # 减少等待时间
        fi
    else
        echo "警告: logmonitor不存在" >&2
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
    
    "$LOGMONITOR_BIN" -c write -n "$log_name" -m "$message" -l "$level"
}

log_error() { log 1 "$1"; }
log_warn()  { log 2 "$1"; }
log_info()  { log 3 "$1"; }
log_debug() { log 4 "$1"; }

# 设置日志文件
set_log_file() {
    [ -z "$1" ] && return 1
    LOG_FILE_NAME="$1"
    return 0
}

# 清理日志
clean_logs() {
    [ "$LOGGER_INITIALIZED" = "1" ] && "$LOGMONITOR_BIN" -c clean
}

# 停止日志系统
stop_logger() {
    if [ "$LOGGER_INITIALIZED" = "1" ] && [ -n "$LOGMONITOR_PID" ]; then
        "$LOGMONITOR_BIN" -c flush  # 停止前确保刷新
        kill -TERM "$LOGMONITOR_PID" 2>/dev/null
        LOGGER_INITIALIZED=0
    fi
}