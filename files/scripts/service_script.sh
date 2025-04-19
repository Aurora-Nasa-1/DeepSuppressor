#!/system/bin/sh

# 设置日志文件
set_log_file "process_manager_service" # 可以考虑改个名字以区分 process_manager 本身的日志
log_info "Starting process manager service script"

# 配置文件路径
CONFIG_FILE="$MODPATH/module_settings/config.sh"
PROCESS_MANAGER="$MODPATH/bin/process_manager"
# 新增：logmonitor 路径和日志目录
LOG_MONITOR="$MODPATH/bin/logmonitor"
LOG_DIR="$MODPATH/logs"

# 确保日志目录存在
mkdir -p "$LOG_DIR"
if [ $? -ne 0 ]; then
    log_error "Failed to create log directory: $LOG_DIR"
    # 可以选择退出或继续，这里选择记录错误并继续
fi

# 从配置文件读取配置
while IFS='=' read -r key value; do
    # 跳过注释行和空行
    [[ "$key" =~ ^# ]] || [[ -z "$key" ]] && continue
    
    # 检查是否是启用配置
    if [[ "$key" =~ ^suppress_com_.*$ ]] && [[ ! "$key" =~ _[0-9]+$ ]] && [ "$value" = "true" ]; then
        # 提取包名（将下划线转回点号）
        package=${key#suppress_}
        package=$(echo "$package" | sed 's/_/./g')
        
        log_info "Found enabled package: $package"

        # 重新读取配置文件来查找对应的进程配置
        while IFS='=' read -r proc_key proc_value; do
            [[ "$proc_key" =~ ^# ]] || [[ -z "$proc_key" ]] && continue
            
            if [[ "$proc_key" =~ ^suppress_config_${key#suppress_}_[0-9]+$ ]] && [[ ! -z "$proc_value" ]]; then
                # 移除引号
                process=$(echo "$proc_value" | tr -d '"')

                # 创建适合文件名的日志名称 (替换所有非字母数字._-为_)
                safe_process_name=$(echo "$process" | sed 's/[^a-zA-Z0-9._-]/_/g')
                log_name="pm_${package}_${safe_process_name}"

                # 构建需要 logmonitor 执行的命令字符串 (注意内部引号)
                # $PROCESS_MANAGER, $package, $process 变量会被展开
                command_to_execute="\"$PROCESS_MANAGER\" \"$package\" \"$process\""

                log_info "Starting process manager for package: $package, process: '$process' via logmonitor (log: $LOG_DIR/${log_name}.log)"

                # 使用 logmonitor 启动 process_manager 并捕获其 stdout/stderr
                # nohup 确保 logmonitor 在后台运行，即使此脚本退出
                # >/dev/null 2>&1 应用于 nohup logmonitor 命令本身，丢弃其直接输出（如果有的话）
                nohup "$LOG_MONITOR" -c execute -d "$LOG_DIR" -n "$log_name" -e "$command_to_execute" >/dev/null 2>&1 &
                pid=$! # 获取 logmonitor 进程的 PID

                # 检查 logmonitor 进程是否成功启动
                if ps -p $pid > /dev/null; then
                    log_info "Logmonitor (for process manager '$process') started successfully with PID: $pid"
                else
                    log_error "Failed to start logmonitor for package $package, process '$process'"
                fi
                # UI 输出保持不变或稍作修改
                Aurora_ui_print "Process manager started for $package - '$process' (logging enabled)"
            fi
        done < "$CONFIG_FILE"
    fi
done < "$CONFIG_FILE"

# 监控配置文件变化
monitor_config() {
    log_info "Starting config file monitoring"
    enter_pause_mode "$CONFIG_FILE" "$MODPATH/scripts/reload_process_manager.sh"
}

# 启动配置监控
monitor_config
