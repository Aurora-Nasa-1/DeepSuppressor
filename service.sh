#!/system/bin/sh
# shellcheck disable=SC1017
sleep 30
# 配置文件路径和可执行文件路径
CONFIG_FILE="$MODPATH/module_settings/suppress_config.json"
PROCESS_MANAGER="$MODPATH/bin/process_manager-DeepSuppressor"
JSON_PARSER="$MODPATH/bin/json_parser-DeepSuppressor"
LOG_DIR="$MODPATH/logs"

# 确保日志目录存在
mkdir -p "$LOG_DIR" || { log_error "Failed to create log directory"; Aurora_abort "PRL" 1;}

ARGS=$($JSON_PARSER "$CONFIG_FILE") || { log_error "Failed to parse JSON"; Aurora_abort "PRL" 1;}
# 启动进程管理器
if [ -x "$PROCESS_MANAGER" ]; then
    $PROCESS_MANAGER -d $ARGS &
    log_info "Process manager started with arguments: $ARGS"
else
    log_error "Process manager not found or not executable"
    Aurora_abort "PRL" 1
fi

# 记录正常退出信息
log_info "Service initialization completed"
