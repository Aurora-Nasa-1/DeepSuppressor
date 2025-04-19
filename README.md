# DeepSuppressor

## 项目简介
**DeepSuppressor 是一个高效的后台进程管理工具，专为Android设备设计，用于监控和控制在后台运行的应用程序进程。**

本模块基于 [Aurora-Magisk-Modules-Framework-2](https://github.com/Aurora-Nasa-1/AMMF2) 开发,你也可以把它看作是一个AMMF模块开发示例(由AI辅助开发)

## 主要功能
- 以低耗电低占用的方式，监控并压制后台进程。
- 方便配置，支持自定义进程列表和监控规则。
- 提供 KernelSU Web UI，方便用户开关某个应用的压制策略。

## 安装
1.  **下载模块:** 从 [Release](https://github.com/Aurora-Nasa-1/DeepSuppressor/releases) 下载最新版本的模块。
3.  **刷入模块:** 将下载的 `.zip` 文件通过 Magisk Manager 或其他 Root 管理工具进行安装。
4.  **重启设备:** 重启您的 Android 设备以激活模块。

## 用户配置
-  **打开 Web UI:** 打开本模块的WebUI，（[KsuWebUI](https://github.com/5ec1cff/KsuWebUIStandalone)或[MMRL](https://github.com/MMRLApp/MMRL)）进行简单配置（开关应用是否压制）

### 详细配置教程
1. **编辑配置文件**
   打开 `module_settings/config.sh` 文件，按照以下格式添加配置：
   ```
   # 启用对某应用的压制
   suppress_APP_包名=true
   
   # 添加要压制的进程（将包名中的点替换为下划线）
   suppress_config_包名_1="进程名1"
   suppress_config_包名_2="进程名2"
   ```
   示例配置：
   ```
   # 压制A114514APP(包名com.a114514.app)
   suppress_APP_com_a114514_app=true
   suppress_config_com_a114514_app_1="com_a114514_app:fvv"
   suppress_config_com_a114514_app_2="com_a114514_app:appbrand114"
   ```
   **欢迎提交 PR 增加更多配置**

3. **注意事项**
   - 修改配置后需要重启生效
   - 包名中的点(.)需要替换为下划线(_)


## 项目结构

- `src/`: 包含 C++ 源代码 (例如 `process_manager.cpp`, `filewatch.cpp`, `logmonitor.cpp`)
- `webroot/`: 包含 Web UI 的前端文件 (HTML, CSS, JavaScript)
- `files/`: 包含脚本和其他文件
- `module_settings/`: 包含模块配置
- `action.sh`, `build.sh`, `customize.sh`, `service.sh`: 项目相关的 Shell 脚本
- `LICENSE`: 项目许可证文件

## 构建

1.  **配置模块 (可选):** 编辑 `module_settings/config.sh` 文件，修改模块的 ID、名称、作者和描述等信息。
2.  **运行构建脚本:** 在项目根目录下打开 Bash 终端，执行：
    ```bash
    bash build.sh
    ```
    *   脚本会自动检查并尝试安装所需的工具和 NDK。
    *   如果 NDK 未找到，脚本会提示您自动下载安装或手动指定路径。
    *   脚本会编译 `src/` 目录下的 C++ 源码 (针对 arm64-v8a 和 x86_64 架构)。
    *   脚本会根据 `module_settings/config.sh` 和 Git 标签 (或用户输入) 生成 `module.prop` 文件。
    *   最终会打包生成一个名为 `[模块名称]_[版本号].zip` 的 Magisk 模块刷机包在项目根目录。
