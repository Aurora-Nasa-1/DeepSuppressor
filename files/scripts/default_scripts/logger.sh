#!/system/bin/sh
# 简化的日志系统 - 使用缓冲区减少进程调用

# 全局变量
LOGGER_INITIALIZED=0
LOG_FILE_NAME=""
LOG_BUFFER=""
LOG_BUFFER_SIZE=0
MAX_BUFFER_SIZE=4096  # 约4KB的缓冲区大小
LAST_FLUSH_TIME=0

# 初始化日志系统
init_logger() {
    # 如果已经初始化，则跳过
    if [ "$LOGGER_INITIALIZED" = "1" ]; then
        return 0
    fi

    # 日志目录
    if [ -z "$LOG_DIR" ]; then
        LOG_DIR="${MODPATH}/logs"
    fi

    # 确保日志目录存在
    mkdir -p "$LOG_DIR" 2>/dev/null || {
        echo "${ERROR_TEXT}: ${ERROR_INVALID_DIR:-无法创建日志目录}: $LOG_DIR" >&2
        return 1
    }

    # 日志级别: 0=关闭 1=错误 2=警告 3=信息 4=调试
    if [ -z "$LOG_LEVEL" ]; then
        LOG_LEVEL=3
    fi
    
    # 检查 logmonitor 可执行文件是否存在
    LOGMONITOR_BIN="${MODPATH}/bin/logmonitor"
    if [ ! -f "$LOGMONITOR_BIN" ]; then
        echo "${WARN_TEXT}: ${SERVICE_FILE_NOT_FOUND}: $LOGMONITOR_BIN" >&2
        echo "${INFO_TEXT}: ${LOG_FALLBACK:-将使用简化的日志功能}" >&2
    else
        # 启动 logmonitor 守护进程 - 使用 daemon 模式
        "$LOGMONITOR_BIN" -c daemon -d "$LOG_DIR" -l "$LOG_LEVEL" >/dev/null 2>&1 &
    fi
    
    # 记录当前时间作为初始刷新时间
    LAST_FLUSH_TIME=$(date +%s)
    
    # 标记为已初始化
    LOGGER_INITIALIZED=1
    log_info "${LOG_INITIALIZED}"
    return 0
}

# 设置日志文件函数
set_log_file() {
    if [ -z "$1" ]; then
        echo "${ERROR_TEXT}: ${WARN_MISSING_PARAMETERS}" >&2
        return 1
    fi

    # 确保日志系统已初始化
    if [ "$LOGGER_INITIALIZED" != "1" ]; then
        init_logger || return 1
    fi

    # 如果切换日志文件，先刷新当前缓冲区
    if [ -n "$LOG_FILE_NAME" ] && [ "$LOG_FILE_NAME" != "$1" ] && [ "$LOG_BUFFER_SIZE" -gt 0 ]; then
        _flush_buffer
    fi

    LOG_FILE_NAME="$1"
    log_info "${LOG_FILE_SET}: $1"
    return 0
}

# 内部缓冲区刷新函数
_flush_buffer() {
    if [ "$LOG_BUFFER_SIZE" -eq 0 ]; then
        return 0
    fi
    
    if [ -f "$LOGMONITOR_BIN" ]; then
            # 创建临时批处理文件
            local batch_file="$TMP_FOLDER/log_batch.tmp"
            echo "$LOG_BUFFER" > "$batch_file"
            # 使用批量写入功能
            "$LOGMONITOR_BIN" -c batch -d "$LOG_DIR" -n "${LOG_FILE_NAME:-system}" -b "$batch_file" -p 2>/dev/null
            rm -f "$batch_file"
    else
        # 简化的日志写入（备用方案）
        echo "$LOG_BUFFER" | while IFS="|" read -r level_num message; do
            local level="INFO"
            case "$level_num" in
                1) level="${ERROR_TEXT:-ERROR}" ;;
                2) level="${WARN_TEXT:-WARN}" ;;
                3) level="${INFO_TEXT:-INFO}" ;;
                4) level="${DEBUG_TEXT:-DEBUG}" ;;
            esac
            local timestamp=$(date "+%Y-%m-%d %H:%M:%S")
            echo "${timestamp} [${level}] ${message}" >> "$LOG_DIR/${LOG_FILE_NAME:-system}.log"
        done
    fi
    
    # 清空缓冲区
    LOG_BUFFER=""
    LOG_BUFFER_SIZE=0
    LAST_FLUSH_TIME=$(date +%s)
    
    return 0
}

# 刷新日志缓冲区
flush_log() {
    _flush_buffer
    
    if [ -f "$LOGMONITOR_BIN" ]; then
        "$LOGMONITOR_BIN" -c flush -d "$LOG_DIR" >/dev/null 2>&1
    fi
    return 0
}

# 日志函数
log_error() {
    [ "$LOG_LEVEL" -ge 1 ] && _write_log 1 "$1"
}

log_warn() {
    [ "$LOG_LEVEL" -ge 2 ] && _write_log 2 "$1"
}

log_info() {
    [ "$LOG_LEVEL" -ge 3 ] && _write_log 3 "$1"
}

log_debug() {
    [ "$LOG_LEVEL" -ge 4 ] && _write_log 4 "$1"
}

# 内部写日志函数 - 使用缓冲区
_write_log() {
    local level_num="$1"
    local message="$2"
    
    # 添加到缓冲区
    LOG_BUFFER="${LOG_BUFFER}${level_num}|${message}
"
    LOG_BUFFER_SIZE=$((LOG_BUFFER_SIZE + 1))
    
    # 检查是否需要刷新缓冲区
    # 1. 缓冲区大小达到阈值
    # 2. 错误级别日志
    # 3. 距离上次刷新超过30秒
    local current_time=$(date +%s)
    local time_diff=$((current_time - LAST_FLUSH_TIME))
    
    if [ "$LOG_BUFFER_SIZE" -ge "$MAX_BUFFER_SIZE" ] || [ "$level_num" -eq 1 ] || [ "$time_diff" -ge 30 ]; then
        _flush_buffer
    fi
}

# 统一的日志接口，兼容旧的log_to_file函数
log_to_file() {
    local level="$1"
    local message="$2"
    
    case "$level" in
        "ERROR") log_error "$message" ;;
        "WARN") log_warn "$message" ;;
        "INFO") log_info "$message" ;;
        "DEBUG") log_debug "$message" ;;
        *) log_info "$message" ;;
    esac
}

# 清理所有日志
clean_logs() {
    # 先刷新缓冲区
    _flush_buffer
    
    if [ -f "$LOGMONITOR_BIN" ]; then
        "$LOGMONITOR_BIN" -c clean -d "$LOG_DIR" >/dev/null 2>&1
    else
        if [ -d "$LOG_DIR" ]; then
            rm -f "$LOG_DIR"/*.log "$LOG_DIR"/*.log.old 2>/dev/null
        fi
    fi
    log_info "${LOG_CLEANED}"
    return 0
}

# 在脚本退出时自动刷新缓冲区
trap flush_log EXIT

# 自动初始化日志系统
init_logger