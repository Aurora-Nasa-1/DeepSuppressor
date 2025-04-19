#!/system/bin/sh

# 设置日志文件
set_log_file "process_manager"
log_info "Starting process manager service"

# 配置文件路径
CONFIG_FILE="$MODPATH/module_settings/config.sh"
PROCESS_MANAGER="$MODPATH/bin/process_manager"

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
                
                log_info "Starting process manager for package: $package, process: $process"
                nohup "$PROCESS_MANAGER" "$package" "$process" >/dev/null 2>&1 &
                pid=$!
                if ps -p $pid > /dev/null; then
                    log_info "Process manager started successfully with PID: $pid"
                else
                    log_error "Failed to start process manager for $package"
                fi
                Aurora_ui_print "Process manager started for $package - $process"
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
