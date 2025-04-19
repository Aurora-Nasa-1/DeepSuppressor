#!/system/bin/sh

log_info "Starting process manager service script (single process mode)"

# 配置文件路径和可执行文件路径
CONFIG_FILE="$MODPATH/module_settings/config.sh"
PROCESS_MANAGER="$MODPATH/bin/process_manager"
LOG_DIR="$MODPATH/logs"
COMBINED_LOG_FILE="$LOG_DIR/process_manager_combined.log"
PID_FILE="$LOG_DIR/process_manager.pid"

# 确保日志目录存在
mkdir -p "$LOG_DIR" || {
    log_error "Failed to create log directory: $LOG_DIR"
    exit 1
}

# 清理旧的日志和PID文件
rm -f "$PID_FILE"

# 构建传递给 process_manager 的参数列表
ARGS=""
TARGET_COUNT=0

# 从配置文件读取配置
while IFS='=' read -r key value; do
    [[ "$key" =~ ^# ]] || [[ -z "$key" ]] && continue

    if [[ "$key" =~ ^suppress_com_.*$ ]] && [[ ! "$key" =~ _[0-9]+$ ]] && [ "$value" = "true" ]; then
        package_key_part=${key#suppress_}
        package=$(echo "$package_key_part" | sed 's/_/./g')

        log_info "Found enabled package: $package"

        while IFS='=' read -r proc_key proc_value; do
            [[ "$proc_key" =~ ^# ]] || [[ -z "$proc_key" ]] && continue

            if [[ "$proc_key" =~ ^suppress_config_${package_key_part}_[0-9]+$ ]] && [[ -n "$proc_value" ]]; then
                process=$(echo "$proc_value" | tr -d '"')
                log_info "Adding target: Package=$package, Process='$process'"
                ARGS="$ARGS \"$package\" \"$process\""
                TARGET_COUNT=$((TARGET_COUNT + 1))
                Aurora_ui_print "Target added: $package - '$process'"
            fi
        done < "$CONFIG_FILE"
    fi
done < "$CONFIG_FILE"

# 检查目标数量
if [ $TARGET_COUNT -eq 0 ]; then
    log_info "No enabled targets found. Exiting."
    Aurora_ui_print "No targets enabled. Process manager not started."
    exit 0
fi

log_info "Collected $TARGET_COUNT targets. Starting process manager..."

# 检查可执行文件
if [ ! -x "$PROCESS_MANAGER" ]; then
    log_error "Process manager executable not found or not executable"
    Aurora_ui_print "Error: process_manager executable missing!"
    exit 1
fi

# 启动进程管理器
start_process() {
    nohup sh -c "exec \"$PROCESS_MANAGER\" $ARGS" > "$COMBINED_LOG_FILE" 2>&1 &
    echo $! > "$PID_FILE"
    sleep 0.5 # 缩短等待时间
    
    local pid=$(cat "$PID_FILE" 2>/dev/null)
    if [ -z "$pid" ] || ! ps -p "$pid" >/dev/null 2>&1; then
        log_error "Process failed to start. Check $COMBINED_LOG_FILE"
        tail -n 20 "$COMBINED_LOG_FILE" >&2
        return 1
    fi
    return 0
}

# 监控进程状态
monitor_process() {
    local pid=$(cat "$PID_FILE")
    while true; do
        if ! ps -p "$pid" >/dev/null 2>&1; then
            log_error "Process manager (PID $pid) died, restarting..."
            if ! start_process; then
                log_error "Failed to restart process manager"
                return 1
            fi
            pid=$(cat "$PID_FILE")
        fi
        sleep 10
    done
}

# 配置监控
monitor_config() {
    log_info "Starting config file monitoring"
    enter_pause_mode "$CONFIG_FILE" "$MODPATH/scripts/reload_process_manager.sh"
}

# 主执行流程
if start_process; then
    pid=$(cat "$PID_FILE")
    log_info "Process manager started with PID: $pid"
    Aurora_ui_print "Process manager started (PID: $pid)"
    
    # 启动监控
    monitor_process &
    MONITOR_PID=$!
    monitor_config
    
    # 清理
    wait $MONITOR_PID
    kill -TERM $pid >/dev/null 2>&1
else
    exit 1
fi

log_info "Service script finished setup"