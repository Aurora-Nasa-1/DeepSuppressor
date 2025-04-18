#!/system/bin/sh

# 设置日志文件
set_log_file "process_manager"
log_info "Reloading process manager configuration"

# 配置文件路径
CONFIG_FILE="$MODPATH/module_settings/config.sh"
PROCESS_MANAGER="$MODPATH/bin/process_manager"

# 终止现有进程管理器
killall process_manager
log_info "Stopped existing process managers"

# 读取配置并重启进程管理器
if [ -f "$CONFIG_FILE" ]; then
    . "$CONFIG_FILE"
    
    if [ -n "$target_package" ] && [ -n "$target_process" ]; then
        log_info "Restarting process manager for package: $target_package, process: $target_process"
        
        # 重启进程管理器
        nohup "$PROCESS_MANAGER" "$target_package" "$target_process" > /dev/null 2>&1 &
        Aurora_ui_print "Process manager restarted for $target_package"
    else
        log_error "Invalid configuration: target_package or target_process not set"
    fi
else
    log_error "Configuration file not found during reload: $CONFIG_FILE"
fi

# 确保日志写入
flush_log