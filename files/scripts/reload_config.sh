#!/system/bin/sh

# 定义路径变量
MODPATH="/data/adb/modules/DeepSuppressor"
CONFIG_FILE="$MODPATH/module_settings/config.sh"
PROCESS_MANAGER="$MODPATH/bin/process_manager"
LOG_DIR="$MODPATH/logs"

# 日志函数
log_info() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') [INFO] $1" >> "$LOG_DIR/reload.log"
}

# 确保日志目录存在
mkdir -p "$LOG_DIR" || { log_info "Failed to create log directory"; exit 1; }

# 从配置文件读取配置并构建参数
load_config() {
    ARGS=""
    while IFS='=' read -r key value || [ -n "$key" ]; do
        # 跳过注释和空行
        case "$key" in
            "#"*|"") continue ;;
        esac

        # 处理suppress_APP_开头的配置项
        case "$key" in
            suppress_APP_*)
                if [ "$value" = "true" ]; then
                    package=${key#suppress_APP_}
                    package=$(echo "$package" | tr '_' '.')
                    ARGS="$ARGS \"$package\""

                    # 收集该包名下的所有进程配置
                    while IFS='=' read -r proc_key proc_value || [ -n "$proc_key" ]; do
                        if echo "$proc_key" | grep -qE "^suppress_config_${key#suppress_APP_}_[0-9]+\$" &&
                            [ -n "$proc_value" ]; then
                            process=$(printf '%s' "$proc_value" | tr -d '"')
                            ARGS="$ARGS \"$process\""
                            log_info "Added process: $process"
                        fi
                    done <"$CONFIG_FILE"
                    log_info "Added package: $package"
                fi
                ;;
        esac
    done <"$CONFIG_FILE"
    echo "$ARGS"
}

# 停止现有进程
stop_services() {
    # 查找并停止进程管理器
    pm_pid=$(pgrep process_manager)
    if [ -n "$pm_pid" ]; then
        kill "$pm_pid" 2>/dev/null
        log_info "Stopped process manager (PID: $pm_pid)"
    fi

    # 查找并停止文件监控进程
    fw_pid=$(pgrep filewatch)
    if [ -n "$fw_pid" ]; then
        kill "$fw_pid" 2>/dev/null
        log_info "Stopped file monitor (PID: $fw_pid)"
    fi

    # 等待进程完全停止
    sleep 2
}

# 启动服务
start_services() {
    local args="$1"
    
    # 启动进程管理器
    if [ -x "$PROCESS_MANAGER" ]; then
        "$PROCESS_MANAGER" -d $args &
        log_info "Process manager started with arguments: $args"
    else
        log_info "Error: Process manager not found or not executable"
        return 1
    fi

    # 启动文件监控
    if [ -f "$MODPATH/bin/filewatch" ]; then
        "$MODPATH/bin/filewatch" -d -i 720 "$CONFIG_FILE" "$MODPATH/files/scripts/reload_config.sh" &
        log_info "File monitor started for config file"
    else
        log_info "Error: filewatch not found or not executable"
        return 1
    fi

    return 0
}

# 主执行流程
log_info "Starting config reload process"
stop_services

# 加载配置并启动服务
ARGS=$(load_config)
if start_services "$ARGS"; then
    log_info "Services started successfully"
else
    log_info "Failed to start services"
    exit 1
fi

log_info "Config reload completed"