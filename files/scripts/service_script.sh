#!/system/bin/sh

# 设置日志文件
set_log_file "process_manager"
log_info "Starting process manager service"

# 配置文件路径
CONFIG_FILE="$MODPATH/module_settings/config.sh"
PROCESS_MANAGER="$MODPATH/bin/process_manager"

# 遍历所有环境变量查找配置
env | while IFS='=' read -r key value; do
    # 检查是否是启用配置
    if [[ "$key" =~ ^suppress_com_.*$ ]] && [[ ! "$key" =~ _[0-9]+$ ]] && [ "$value" = "true" ]; then
        # 提取包名（将下划线转回点号）
        package=${key#suppress_}
        package=$(echo "$package" | tr '_' '.')

        # 遍历该应用的所有进程配置
        i=1
        while true; do
            process_key="suppress_config_${key#suppress_}_${i}"
            process=${!process_key}

            # 如果没有更多进程配置则退出循环
            [ -z "$process" ] && break

            log_info "Starting process manager for package: $package, process: $process"
            nohup "$PROCESS_MANAGER" "$package" "$process" >/dev/null 2>&1 &
            Aurora_ui_print "Process manager started for $package - $process"

            i=$((i + 1))
        done
    fi
done

# 监控配置文件变化
monitor_config() {
    log_info "Starting config file monitoring"
    enter_pause_mode "$CONFIG_FILE" "$MODPATH/scripts/reload_process_manager.sh"
}

# 启动配置监控
monitor_config
