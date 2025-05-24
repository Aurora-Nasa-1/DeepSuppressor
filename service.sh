#!/system/bin/sh
sleep 30
# 配置文件路径和可执行文件路径
CONFIG_FILE="$MODPATH/module_settings/config.sh"
PROCESS_MANAGER="$MODPATH/bin/process_manager"
LOG_DIR="$MODPATH/logs"

# 确保日志目录存在
mkdir -p "$LOG_DIR" || { log_info "Failed to create log directory"; exit 1; }

# 从配置文件读取配置并构建参数
ARGS=""
while IFS='=' read -r key value || [ -n "$key" ]; do  # 处理文件末尾没有换行的情况
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

# 启动进程管理器
if [ -x "$PROCESS_MANAGER" ]; then
    "$PROCESS_MANAGER" -d $ARGS &
    log_info "Process manager started with arguments: $ARGS"
else
    log_info "Error: Process manager not found or not executable"
    exit 1
fi

# 启动文件监控
if [ -f "$MODPATH/bin/filewatch" ]; then
    "$MODPATH/bin/filewatch" -d -i 720 "$CONFIG_FILE" "$MODPATH/files/scripts/reload_config.sh" &
    log_info "File monitor started for config file"
else
    log_info "Error: filewatch not found or not executable"
    exit 1
fi

# 记录正常退出信息
log_info "Service initialization completed"
