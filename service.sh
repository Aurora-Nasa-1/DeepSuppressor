#!/system/bin/sh
MODDIR=${0%/*}
MODPATH="$MODDIR"
# 定义状态文件路径
STATUS_FILE="$MODPATH/status.txt"

# 设置初始状态为"运行中"
echo "RUNNING" >"$STATUS_FILE"

# 初始化日志目录
LOG_DIR="$MODPATH/logs"
mkdir -p "$LOG_DIR" || {
    echo "${ERROR_INVALID_DIR}: $LOG_DIR" >&2
    exit 1
}

# 启动日志监控器
if [ -f "$MODPATH/bin/logmonitor" ]; then
    "$MODPATH/bin/logmonitor" -c start -d "$LOG_DIR" >/dev/null 2>&1 &
    # 记录启动信息
    "$MODPATH/bin/logmonitor" -c write -n "service" -m "LOGSTART" -l 3 >/dev/null 2>&1
fi

# 加载主脚本
if [ ! -f "$MODPATH/files/scripts/default_scripts/main.sh" ]; then
    echo "Error NotFound: $MODPATH/files/scripts/default_scripts/main.sh" > "$LOG_DIR/error.log"
    exit 1
else
    . "$MODPATH/files/scripts/default_scripts/main.sh"
    # 设置service脚本的日志文件
    start_script
    set_log_file "service"
    log_info "${SERVICE_LOADING_MAIN:-加载 main.sh}"
fi

# 定义状态更新函数
update_status() {
    echo "$1" >"$STATUS_FILE"
    log_info "${SERVICE_STATUS_UPDATE:-状态已更新}: $1"
}

# 定义abort函数，与main.sh中的Aurora_abort保持一致
abort() {
    log_error "$1"
    update_status "ERROR"
    exit 1
}

# 进入暂停模式的函数
enter_pause_mode() {
    update_status "PAUSED"
    log_info "${SERVICE_PAUSED:-已进入暂停模式，监控文件}: $1"
    
    # 检查参数数量
    if [ "$#" -eq 2 ]; then
        # 如果是两个参数，第二个参数是脚本路径
        log_debug "Use Script: $2"
        if [ -f "$MODPATH/bin/filewatch" ]; then
            "$MODPATH/bin/filewatch" -s "$STATUS_FILE" "$1" "$2"
        else
            log_error "filewatch$SERVICE_FILE_NOT_FOUND"
            update_status "ERROR"
        fi
    elif [ "$#" -eq 3 ] && [ "$2" = "-c" ]; then
        # 如果是三个参数且第二个是-c，第三个参数是shell命令
        log_debug "使用shell命令: $3"
        if [ -f "$MODPATH/bin/filewatch" ]; then
            "$MODPATH/bin/filewatch" -s "$STATUS_FILE" -c "$3" "$1"
        else
            log_error "filewatch$SERVICE_FILE_NOT_FOUND"
            update_status "ERROR"
        fi
    else
        log_error "enter_pause_mode的参数无效"
        update_status "ERROR"
    fi
}

=

# 记录启动信息
log_info "${SERVICE_STARTED:-服务已启动}"
start_script

# 加载服务脚本
if [ ! -f "$MODPATH/files/scripts/service_script.sh" ]; then
    log_error "${SERVICE_FILE_NOT_FOUND:-文件未找到}: $MODPATH/files/scripts/service_script.sh"
    abort "文件未找到!!!($MODPATH/files/scripts/service_script.sh)"
else
    log_info "${SERVICE_LOADING_SERVICE_SCRIPT:-正在加载service_script.sh}"
    . "$MODPATH/files/scripts/service_script.sh"
fi

# 记录正常退出信息
log_info "${SERVICE_NORMAL_EXIT:-服务正常退出}"
# 脚本结束前更新状态为正常退出
update_status "NORMAL_EXIT"

stop_logger