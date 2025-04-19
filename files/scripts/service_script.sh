#!/system/bin/sh

# 配置文件路径和可执行文件路径
CONFIG_FILE="$MODPATH/module_settings/config.sh"
PROCESS_MANAGER="$MODPATH/bin/process_manager"
LOG_DIR="$MODPATH/logs"

# 确保日志目录存在
mkdir -p "$LOG_DIR" || exit 1

# 从配置文件读取配置并构建参数
ARGS=""
while IFS='=' read -r key value; do
    # 跳过注释和空行
    [[ "$key" =~ ^#|^$ ]] && continue
    
    # 处理suppress_APP_开头的配置项
    if [[ "$key" =~ ^suppress_APP_ ]] && [[ "$value" == "true" ]]; then
        package=${key#suppress_APP_}
        package=${package//_/.}
        ARGS="$ARGS \"$package\""
        
        # 收集该包名下的所有进程配置
        while IFS='=' read -r proc_key proc_value; do
            if [[ "$proc_key" =~ ^suppress_config_${key#suppress_APP_}_[0-9]+$ ]] && 
               [[ -n "$proc_value" ]] && 
               [[ ! "$proc_value" =~ ^# ]]; then
                process=$(echo "$proc_value" | tr -d '"')
                ARGS="$ARGS \"$process\""
            fi
        done < "$CONFIG_FILE"
    fi
done < "$CONFIG_FILE"

# 启动进程管理器（不再需要重定向到日志文件，因为程序内部已经处理日志）
nohup sh -c "exec \"$PROCESS_MANAGER\" $ARGS" >/dev/null 2>&1 &