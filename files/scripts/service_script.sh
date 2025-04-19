#!/system/bin/sh


log_info "Starting process manager service script (single process mode)"

# 配置文件路径和可执行文件路径
CONFIG_FILE="$MODPATH/module_settings/config.sh"
PROCESS_MANAGER="$MODPATH/bin/process_manager"
LOG_DIR="$MODPATH/logs" # C++ 进程的输出日志目录
COMBINED_LOG_FILE="$LOG_DIR/process_manager_combined.log" # C++ 进程的日志文件

# 确保日志目录存在
mkdir -p "$LOG_DIR"
if [ $? -ne 0 ]; then
    log_error "Failed to create log directory: $LOG_DIR"
    # 可以选择退出
    exit 1
fi

# 清理旧的组合日志文件（可选）
# rm -f "$COMBINED_LOG_FILE"

# 构建传递给 process_manager 的参数列表
ARGS=""
TARGET_COUNT=0

# 从配置文件读取配置
while IFS='=' read -r key value; do
    # 跳过注释行和空行
    [[ "$key" =~ ^# ]] || [[ -z "$key" ]] && continue

    # 检查是否是启用配置
    if [[ "$key" =~ ^suppress_com_.*$ ]] && [[ ! "$key" =~ _[0-9]+$ ]] && [ "$value" = "true" ]; then
        # 提取包名（将下划线转回点号）
        package_key_part=${key#suppress_}
        package=$(echo "$package_key_part" | sed 's/_/./g')

        log_info "Found enabled package: $package"

        # 查找对应的进程配置
        while IFS='=' read -r proc_key proc_value; do
            [[ "$proc_key" =~ ^# ]] || [[ -z "$proc_key" ]] && continue

            # 匹配与当前启用的包相关的进程配置
            # suppress_config_ + package_key_part (带下划线的) + _ + 数字
            if [[ "$proc_key" =~ ^suppress_config_${package_key_part}_[0-9]+$ ]] && [[ ! -z "$proc_value" ]]; then
                # 移除引号
                process=$(echo "$proc_value" | tr -d '"')

                log_info "Adding target: Package=$package, Process='$process'"
                # 将包名和进程名添加到参数列表，注意引号处理
                ARGS="$ARGS \"$package\" \"$process\""
                TARGET_COUNT=$((TARGET_COUNT + 1))

                # UI 输出
                Aurora_ui_print "Target added: $package - '$process'"
            fi
        done < "$CONFIG_FILE"
    fi
done < "$CONFIG_FILE"

# 检查是否找到了任何目标
if [ $TARGET_COUNT -eq 0 ]; then
    log_info "No enabled targets found in $CONFIG_FILE. Exiting."
    Aurora_ui_print "No targets enabled. Process manager not started."
    exit 0
fi

log_info "Collected $TARGET_COUNT targets. Starting single process manager..."

# 检查 process_manager 是否存在且可执行
if [ ! -x "$PROCESS_MANAGER" ]; then
    log_error "Process manager executable not found or not executable: $PROCESS_MANAGER"
    Aurora_ui_print "Error: process_manager executable missing!"
    exit 1
fi

# 启动单个 process_manager 实例，传递所有参数
# 使用 nohup 在后台运行，并将 stdout 和 stderr 重定向到组合日志文件
# 注意：eval 用于正确处理 $ARGS 中的引号
nohup sh -c "exec \"$PROCESS_MANAGER\" $ARGS" > "$COMBINED_LOG_FILE" 2>&1 &
PID=$!

# 检查进程是否成功启动
# 等待一小段时间让进程有机会启动或失败
sleep 1
if ps -p $PID > /dev/null; then
    log_info "Process manager started successfully with PID: $PID. Logging to $COMBINED_LOG_FILE"
    Aurora_ui_print "Process manager started (PID: $PID). See logs for details."
else
    log_error "Failed to start process manager. Check $COMBINED_LOG_FILE for errors."
    Aurora_ui_print "Error: Failed to start process manager!"
    # 可以考虑输出日志文件的最后几行帮助调试
    # tail -n 10 "$COMBINED_LOG_FILE"
fi

# 监控配置文件变化 (这部分逻辑保持不变，但 reload 脚本需要调整)
monitor_config() {
    log_info "Starting config file monitoring"
    # 假设 enter_pause_mode 和 reload_process_manager.sh 存在且功能正确
    # reload_process_manager.sh 现在需要知道如何杀死旧的 PID 并用新的 ARGS 重启
    enter_pause_mode "$CONFIG_FILE" "$MODPATH/scripts/reload_process_manager.sh"
}

# 启动配置监控
monitor_config

log_info "Service script finished setup. Process manager running in background."
