# AMMF 脚本开发指南

## 📋 概述

本文档提供了在AMMF框架内开发自定义脚本的详细说明。它涵盖了可用的函数、变量以及创建安装脚本、服务脚本和用户脚本的最佳实践。AMMF2版本对脚本系统进行了全面更新，增强了日志系统和文件监控功能。

## 🛠️ 脚本类型

1. **安装脚本** (`files/scripts/install_custom_script.sh`)
   - 在模块安装过程中执行
   - 用于设置任务、文件提取和初始配置
   - 可以访问所有AMMF核心函数

2. **服务脚本** (`files/scripts/service_script.sh`)
   - 在设备启动时执行
   - 用于后台服务、监控和运行时操作
   - 支持文件监控和状态管理

3. **Action脚本**
   - 用户点按执行
   - 用于用户交互和自定义功能

## 📚 可用函数

AMMF框架提供了几个有用的函数，可以在您的脚本中使用：

### 用户交互函数

#### `select_on_magisk [input_path]`

使用音量键导航向用户呈现选择菜单（首字母选择）。

**参数：**
- `input_path`：包含选项的文本文件路径（文件内容每行一个选项）

**返回：**
- 在`$SELECT_OUTPUT`变量中返回所选选项

**兼容性：**
- 可以在安装脚本和用户脚本中使用（在用户脚本中谨慎使用）
- 不支持特殊字符或中文（兼容`/][{};:><?!()_-+=.`）

**示例：**
```bash
# 创建一个包含选项的文件
echo "选项1\n选项2\n选项3" > "$MODPATH/options.txt"

# 调用函数
select_on_magisk "$MODPATH/options.txt"

# 使用所选选项
echo "用户选择了: $SELECT_OUTPUT"
```

#### `list_select [input_path] [TITLE]`

使用音量键导航向用户呈现选择菜单。

**参数：**
- `input_path`：包含选项的文本文件路径（文件内容每行一个选项）
- `TITLE`：菜单标题

**返回：**
- 在`$SELECT_OUTPUT`变量中返回所选选项

**兼容性：**
- 可以在安装脚本和用户脚本中使用（在用户脚本中谨慎使用）

**示例：**
```bash
# 创建一个包含选项的文件
echo "选项1\n选项2\n选项3" > "$MODPATH/options.txt"

# 调用函数
list_select "$MODPATH/options.txt" "文本选择"

# 使用所选选项
echo "用户选择了: $SELECT_OUTPUT"
```

#### `number_select [input_path]`

向用户呈现编号选择菜单。

**参数：**
- `input_path`：包含选项的文本文件路径（文件内容每行一个选项）

**返回：**
- 在`$SELECT_OUTPUT`变量中返回所选编号

**兼容性：**
- 仅支持在用户脚本中使用

**示例：**
```bash
# 创建一个包含选项的文件
echo "选项1\n选项2\n选项3" > "$MODPATH/options.txt"

# 调用函数
number_select "$MODPATH/options.txt"

# 使用所选编号
echo "用户选择了选项编号: $SELECT_OUTPUT"
```

#### `key_select`

等待用户按下音量键（上/下）。

**返回：**
- 在`$key_pressed`变量中返回按下的键（`KEY_VOLUMEUP`或`KEY_VOLUMEDOWN`）

**兼容性：**
- 可以在安装脚本和用户脚本中使用（在用户脚本中谨慎使用）

**示例：**
```bash
echo "按音量上键继续或音量下键取消"
key_select

if [ "$key_pressed" = "KEY_VOLUMEUP" ]; then
    echo "继续..."
else
    echo "已取消"
    exit 1
fi
```

### 文件操作

#### `download_file [url]`

从指定URL下载文件。

**参数：**
- `url`：要下载的文件的URL

**行为：**
- 将文件下载到`settings.sh`中`$download_destination`指定的目录
- 如果目录不存在，则创建目录
- 支持下载失败重试和用户交互

**兼容性：**
- 可以在安装脚本和用户脚本中使用

**示例：**
```bash
# 设置下载目的地
download_destination="/storage/emulated/0/Download/AMMF"

# 下载文件
download_file "https://example.com/file.zip"
```

### 服务管理

#### `enter_pause_mode [monitored_file] [execution_script]`

进入暂停模式并使用`filewatch`工具监控文件变化。

**参数：**
- `monitored_file`：要监控的文件路径
- `execution_script`：文件变化时要执行的脚本路径

**行为：**
- 将状态更新为"PAUSED"
- 使用高效的inotify机制监控指定文件的变化
- 检测到变化时执行指定脚本或自定义命令

**兼容性：**
- 用于服务脚本

**示例：**
```bash
# 监控配置文件变化
enter_pause_mode "$MODPATH/module_settings/config.sh" "$MODPATH/scripts/reload_config.sh"
# 或
enter_pause_mode "$MODPATH/module_settings/config.sh" -c "cp $MODPATH/module_settings/config.sh $MODPATH/module_settings/config.sh.bak"
```

### 日志系统

#### `set_log_file [log_name]`

设置当前脚本的日志文件名。

**参数：**
- `log_name`：日志文件名（不包含扩展名）

**行为：**
- 设置后续日志记录的目标文件

**示例：**
```bash
# 设置日志文件
set_log_file "custom_script"
```

#### `log_info [message]`

记录信息级别的日志消息。

**参数：**
- `message`：要记录的消息

**示例：**
```bash
log_info "开始执行自定义操作"
```

#### `log_error [message]`

记录错误级别的日志消息。

**参数：**
- `message`：要记录的错误消息

**示例：**
```bash
log_error "操作失败：无法访问文件"
```

#### `log_warn [message]`

记录警告级别的日志消息。

**参数：**
- `message`：要记录的警告消息

**示例：**
```bash
log_warn "配置文件格式可能不正确"
```

#### `log_debug [message]`

记录调试级别的日志消息。

**参数：**
- `message`：要记录的调试消息

**示例：**
```bash
log_debug "变量值：$variable"
```

#### `flush_log`

强制将日志缓冲区写入磁盘。

**示例：**
```bash
# 执行关键操作后刷新日志
flush_log
```

### 实用函数

#### `Aurora_ui_print [message]`

向控制台打印格式化消息并记录到日志。

**参数：**
- `message`：要打印的消息

**示例：**
```bash
Aurora_ui_print "开始安装..."
```

#### `Aurora_abort [message] [error_code]`

使用错误消息和代码中止脚本，并记录到日志。

**参数：**
- `message`：错误消息
- `error_code`：错误代码

**示例：**
```bash
Aurora_abort "安装失败" 1
```

#### `check_network`

检查网络连接状态。

**返回：**
- 在`$Internet_CONN`变量中返回连接状态（0=无连接，1=仅中国网络，2=GitHub可访问，3=Google可访问）

**示例：**
```bash
check_network
if [ -z "$Internet_CONN" ]; then
    Aurora_ui_print "无网络连接，跳过下载"
fi
```

#### `replace_module_id [file_path] [file_description]`

替换指定文件中的模块ID占位符。

**参数：**
- `file_path`：文件路径
- `file_description`：文件描述（用于日志记录）

**示例：**
```bash
replace_module_id "$MODPATH/files/languages.sh" "languages.sh"
```

## 🌐 可用变量

以下变量可在您的脚本中使用：

### 路径变量

- `$MODPATH`：模块目录路径
- `$MODDIR`：与`$MODPATH`相同
- `$NOW_PATH`：当前脚本执行路径
- `$TMP_FOLDER`：临时文件夹路径（`$MODPATH/TEMP`）
- `$SDCARD`：内部存储路径（`/storage/emulated/0`）
- `$download_destination`：下载文件的默认目录
- `$LOG_DIR`：日志目录路径

### 模块信息

- `$action_id`：模块ID
- `$action_name`：模块名称
- `$action_author`：模块作者
- `$action_description`：模块描述

### 状态变量

- `$STATUS_FILE`：状态文件路径
- `$SH_ON_MAGISK`：指示脚本是否在Magisk上运行的标志
- `$LOG_LEVEL`：当前日志级别（0=关闭，1=错误，2=警告，3=信息，4=调试）
- `$Internet_CONN`：网络连接状态

## 📝 脚本模板

### 安装脚本模板

```bash
#!/system/bin/sh

# 自定义安装脚本
# 在模块安装过程中执行

# 设置日志文件
set_log_file "install_custom"
log_info "开始执行自定义安装脚本"

# 示例：创建必要的目录
mkdir -p "$MODPATH/data"

# 示例：检查网络并下载额外文件
check_network
if [ -n "$Internet_CONN" ]; then
    download_file "https://example.com/extra_file.zip"
else
    log_warn "无网络连接，跳过下载"
fi

# 示例：用户交互
echo "是否启用高级功能？"
echo "按音量上键选择是，音量下键选择否"
key_select

if [ "$key_pressed" = "KEY_VOLUMEUP" ]; then
    # 启用高级功能
    echo "advanced_features=true" >> "$MODPATH/module_settings/settings.json"
    Aurora_ui_print "已启用高级功能"
    log_info "用户选择启用高级功能"
else
    # 禁用高级功能
    echo "advanced_features=false" >> "$MODPATH/module_settings/settings.json"
    Aurora_ui_print "已禁用高级功能"
    log_info "用户选择禁用高级功能"
fi

# 确保日志被写入
flush_log
```

### 服务脚本模板

```bash
#!/system/bin/sh

# 服务脚本
# 在设备启动时执行

# 设置日志文件
set_log_file "service_custom"
log_info "开始执行自定义服务脚本"

# 启动后台服务
start_background_service() {
    # 服务实现
    nohup some_command > /dev/null 2>&1 &
    log_info "后台服务已启动"
    Aurora_ui_print "后台服务已启动"
}

# 监控配置文件变化
monitor_config() {
    log_info "开始监控配置文件变化"
    enter_pause_mode "$MODPATH/module_settings/config.sh" "$MODPATH/scripts/reload_config.sh"
}

# 执行函数
start_background_service
monitor_config

# 确保日志被写入
flush_log
```

## 🔧 最佳实践

1. **错误处理**
   - 始终检查错误并提供有意义的错误消息
   - 对关键错误使用`Aurora_abort`
   - 使用日志系统记录错误详情

2. **文件路径**
   - 使用带有变量的绝对路径，如`$MODPATH`
   - 在`$TMP_FOLDER`中创建临时文件
   - 检查文件是否存在后再访问

3. **用户交互**
   - 在请求用户输入时提供清晰的指示
   - 根据脚本类型使用适当的函数
   - 记录用户选择到日志中

4. **日志记录**
   - 为每个脚本设置唯一的日志文件名
   - 使用适当的日志级别（error, warn, info, debug）
   - 在关键操作后使用`flush_log`确保日志被写入

5. **清理**
   - 不再需要时删除临时文件
   - 正确处理服务终止
   - 避免留下未使用的资源

6. **兼容性**
   - 检查所需的工具和依赖项
   - 根据Android版本或root解决方案使用条件逻辑
   - 测试在不同设备上的行为

## 📋 调试技巧

1. **使用日志系统**
   - 使用`log_debug`记录变量值和执行流程
   - 设置`LOG_LEVEL=4`启用详细调试日志
   - 检查`$LOG_DIR`目录中的日志文件

2. **检查状态**
   - 监控状态文件：`cat "$STATUS_FILE"`
   - 使用`Aurora_ui_print`输出关键状态信息

3. **测试函数**
   - 使用示例输入测试单个函数
   - 在测试前后添加日志标记

4. **检查权限**
   - 确保脚本具有适当的执行权限：`chmod +x script.sh`
   - 检查文件访问权限

5. **验证路径**
   - 在访问文件路径之前验证其是否存在
   - 使用`ls -la`检查文件属性

## 🔄 版本兼容性

在升级AMMF框架时，请注意以下文件的变化：

1. `files/scripts/default_scripts/main.sh` - 核心函数可能会改变
2. `files/scripts/default_scripts/logger.sh` - 日志系统可能会更新
3. `files/languages.sh` - 语言字符串可能会更新

升级后，请检查您的自定义脚本是否与新版本兼容，特别是对于使用了框架内部函数的脚本。

## 🔍 高级功能

### 文件监控系统

AMMF2引入了基于inotify的高效文件监控系统，通过`filewatch`工具实现。该工具支持以下功能：

- 实时监控文件变化
- 低功耗模式选项
- 守护进程模式
- 自定义执行脚本或命令

**高级用法示例：**

```bash
# 在低功耗模式下监控配置文件
"$MODPATH/bin/filewatch" -d -l -i 5 "$MODPATH/module_settings/config.sh" "$MODPATH/scripts/reload_config.sh"
```

### 增强的日志系统

AMMF2包含了一个强大的日志系统，通过`logmonitor`工具实现。该系统提供：

- 多级日志（ERROR, WARN, INFO, DEBUG）
- 自动日志轮转
- 缓冲写入以提高性能
- 按模块分离日志文件

**高级用法示例：**

```bash
# 手动控制日志系统
"$MODPATH/bin/logmonitor" -c write -n "custom_module" -l 3 -m "自定义日志消息"
```

## 📚 参考资源

- [AMMF GitHub 仓库](https://github.com/Aurora-Nasa-1/AMMF2)
- [Magisk 模块开发文档](https://topjohnwu.github.io/Magisk/guides.html)
- [Shell 脚本编程指南](https://www.gnu.org/software/bash/manual/bash.html)

---

如有任何问题或建议，请在GitHub仓库提交issue或联系模块作者。