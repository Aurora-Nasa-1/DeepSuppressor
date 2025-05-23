# DeepSuppressor

## 项目简介
**DeepSuppressor 是一个高效的后台进程管理工具，专为Android设备设计，用于监控和控制在后台运行的应用程序进程。**

本模块基于 [Aurora-Magisk-Modules-Framework-2](https://github.com/Aurora-Nasa-1/AMMF2) 开发,你也可以把它看作是一个AMMF模块开发示例(由AI辅助开发)

**[Telegram](https://t.me/AuroraNasaModule)**

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
