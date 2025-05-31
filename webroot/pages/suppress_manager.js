/**
 * AMMF WebUI 抑制管理器页面模块
 * 管理DeepSuppressor应用抑制配置
 */

const SuppressManagerPage = {
    // 状态和数据
    configData: {},
    configBackup: {}, // 添加配置备份
    installedApps: [],
    isLoading: false,
    searchQuery: '',
    selectedApp: null,
    editingApp: null,
    hasUnsavedChanges: false, // 添加未保存更改标志
    tempConfigState: {}, // 添加临时配置状态
    
    // 配置项
    config: {
        configPath: 'module_settings/suppress_config.json',
        refreshInterval: 0, // 不自动刷新
        showSystemApps: false // 默认不显示系统应用
    },

    // 预加载数据
    async preloadData() {
        try {
            const [configData, installedApps] = await Promise.all([
                this.loadConfig(),
                this.loadInstalledApps()
            ]);
            return { configData, installedApps };
        } catch (error) {
            console.error('预加载抑制管理器数据失败:', error);
            // 使用模拟数据
            return { 
                configData: this.getMockConfigData(), 
                installedApps: this.getMockInstalledApps() 
            };
        }
    },

    // 初始化
    async init() {
        try {
            // 获取预加载的数据或重新加载
            const preloadedData = PreloadManager.getData('suppress_manager') || await this.preloadData();
            this.configData = preloadedData.configData || {};
            this.installedApps = preloadedData.installedApps || [];
            
            // 创建配置备份
            this.createConfigBackup();
            
            // 注册操作按钮和语言切换处理器
            this.registerActions();
            I18n.registerLanguageChangeHandler(this.onLanguageChanged.bind(this));
            return true;
        } catch (error) {
            console.error('初始化抑制管理器页面失败:', error);
            return false;
        }
    },
        
        // 加载配置
    async loadConfig() {
        try {
            const configData = await Core.execCommand(`cat ${this.config.configPath}`);
            const parsedData = JSON.parse(configData) || { suppress_apps: {} };
            return parsedData;
        } catch (error) {
            console.error('加载配置失败:', error);
            // 使用模拟配置数据
            return this.getMockConfigData();
        }
    },

    // 加载已安装应用
    async loadInstalledApps() {
        try {
            const appListData = await Core.execCommand('pm list packages -3');
            const systemAppsData = await Core.execCommand('pm list packages -s');
            
            // 解析第三方应用
            const thirdPartyApps = appListData.split('\n')
                .filter(line => line.trim().startsWith('package:'))
                .map(line => line.trim().substring(8));
                
            // 解析系统应用
            const systemApps = systemAppsData.split('\n')
                .filter(line => line.trim().startsWith('package:'))
                .map(line => line.trim().substring(8));
                
            // 合并并排序
            const allApps = [...thirdPartyApps, ...systemApps].sort();
            
            // 获取应用名称和图标
            const appsWithInfo = await Promise.all(allApps.map(async (packageName) => {
                try {
                    const appInfo = await Core.execCommand(`dumpsys package ${packageName} | grep "versionName\\|applicationInfo"`);
                    const nameMatch = appInfo.match(/labelRes=\d+ nonLocalizedLabel=([^ ]+)/);
                    const versionMatch = appInfo.match(/versionName=([^ ]+)/);
                    const iconPath = await this.getAppIconPath(packageName);
                    
                    return {
                        packageName,
                        appName: nameMatch ? nameMatch[1] : packageName,
                        versionName: versionMatch ? versionMatch[1] : '未知',
                        isSystem: systemApps.includes(packageName),
                        iconPath: iconPath
                    };
                } catch (error) {
                    return {
                        packageName,
                        appName: packageName,
                        versionName: '未知',
                        isSystem: systemApps.includes(packageName),
                        iconPath: null
                    };
                }
            }));
            
            return appsWithInfo;
        } catch (error) {
            console.error('加载已安装应用失败:', error);
            // 使用模拟应用数据
            return this.getMockInstalledApps();
        }
    },
    
    // 获取应用图标路径
    async getAppIconPath(packageName) {
        try {
            const iconPathCommand = `pm path ${packageName} | grep base.apk | head -n 1`;
            const apkPath = (await Core.execCommand(iconPathCommand)).trim().replace('package:', '');
            
            if (!apkPath) return null;
            
            // 使用相对路径构建图标URL
            return `file://${apkPath}`;
        } catch (error) {
            console.error(`获取应用 ${packageName} 图标路径失败:`, error);
            return null;
        }
    },
    
    // 获取模拟配置数据
    getMockConfigData() {
        return {
            suppress_apps: {
                "com.tencent.mm": {
                    enabled: true,
                    processes: ["com.tencent.mm:appbrand0", "com.tencent.mm:appbrand1"]
                },
                "com.tencent.mobileqq": {
                    enabled: false,
                    processes: ["com.tencent.mobileqq:mini"]
                },
                "com.android.chrome": {
                    enabled: true,
                    processes: ["com.android.chrome:sandboxed_process0"]
                }
            }
        };
    },
    
    // 获取模拟已安装应用数据
    getMockInstalledApps() {
        return [
            {
                packageName: "com.tencent.mm",
                appName: "微信",
                versionName: "8.0.30",
                isSystem: false,
                iconPath: null
            },
            {
                packageName: "com.tencent.mobileqq",
                appName: "QQ",
                versionName: "8.9.33",
                isSystem: false,
                iconPath: null
            },
            {
                packageName: "com.android.chrome",
                appName: "Chrome",
                versionName: "103.0.5060.71",
                isSystem: true,
                iconPath: null
            },
            {
                packageName: "com.android.vending",
                appName: "Google Play",
                versionName: "30.6.19",
                isSystem: true,
                iconPath: null
            },
            {
                packageName: "com.xiaomi.market",
                appName: "应用商店",
                versionName: "1.9.8",
                isSystem: true,
                iconPath: null
            }
        ];
    },
    
    // 保存配置
    async saveConfig() {
        try {
            this.showLoading();
            
            // 检查配置正确性
            if (!this.validateConfig()) {
                Core.showToast(I18n.translate('CONFIG_INVALID', '配置无效，请检查'), 'error');
                this.hideLoading();
                return false;
            }
            
            // 格式化JSON并写入文件
            const configJson = JSON.stringify(this.configData, null, 2);
            
            try {
                // 尝试解析以确保JSON有效
                JSON.parse(configJson);
            } catch (e) {
                Core.showToast(I18n.translate('CONFIG_JSON_ERROR', 'JSON 格式错误'), 'error');
                this.hideLoading();
                return false;
            }
            
            // 备份当前配置
            await Core.execCommand(`cp ${this.config.configPath} ${this.config.configPath}.bak`);
            
            // 写入新配置
            await Core.execCommand(`echo '${configJson}' > ${this.config.configPath}`);
            
            // 重启服务以应用配置
            await Core.execCommand('sh ${Core.MODULE_PATH}/service.sh restart');
            
            // 更新备份
            this.createConfigBackup();
            
            // 重置未保存更改标志
            this.hasUnsavedChanges = false;
            
            // 清除临时配置状态
            this.tempConfigState = {};
            
            Core.showToast(I18n.translate('CONFIG_SAVED', '配置已保存并应用'), 'success');
            return true;
        } catch (error) {
            console.error('保存配置失败:', error);
            Core.showToast(I18n.translate('CONFIG_SAVE_ERROR', '保存配置失败'), 'error');
            
            // 尝试恢复备份
            try {
                await Core.execCommand(`cp ${this.config.configPath}.bak ${this.config.configPath}`);
            } catch (backupError) {
                console.error('恢复备份失败:', backupError);
            }
            
            return false;
        } finally {
            this.hideLoading();
        }
    },
    
    // 验证配置
    validateConfig() {
        if (!this.configData || !this.configData.suppress_apps) {
            return false;
        }
        
        // 检查每个应用配置
        for (const [packageName, config] of Object.entries(this.configData.suppress_apps)) {
            if (!packageName || !config) return false;
            
            // 检查进程列表
            if (!Array.isArray(config.processes) || config.processes.length === 0) {
                return false;
            }
            
            // 检查进程名格式
            for (const process of config.processes) {
                if (!process || typeof process !== 'string' || process.trim() === '') {
                    return false;
                }
            }
        }
        
        return true;
    },
    
    // 添加应用配置
    addAppConfig(packageName, processNames = []) {
        if (!this.configData.suppress_apps) {
            this.configData.suppress_apps = {};
        }
        
        this.configData.suppress_apps[packageName] = {
            enabled: true,
            processes: processNames.length > 0 ? processNames : [`${packageName}:appbrand0`]
        };
        
        this.updateDisplay();
    },
    
    // 移除应用配置
    removeAppConfig(packageName) {
        if (this.configData.suppress_apps && this.configData.suppress_apps[packageName]) {
            delete this.configData.suppress_apps[packageName];
            this.updateDisplay();
        }
    },
    
    // 切换应用启用状态
    toggleAppEnabled(packageName) {
        if (this.configData.suppress_apps && this.configData.suppress_apps[packageName]) {
            this.configData.suppress_apps[packageName].enabled = 
                !this.configData.suppress_apps[packageName].enabled;
            this.updateDisplay();
        }
    },
    
    // 添加进程到应用配置
    addProcess(packageName, processName) {
        if (this.configData.suppress_apps && this.configData.suppress_apps[packageName]) {
            if (!this.configData.suppress_apps[packageName].processes.includes(processName)) {
                this.configData.suppress_apps[packageName].processes.push(processName);
                this.updateDisplay();
            }
        }
    },
    
    // 从应用配置中移除进程
    removeProcess(packageName, processName) {
        if (this.configData.suppress_apps && this.configData.suppress_apps[packageName]) {
            const index = this.configData.suppress_apps[packageName].processes.indexOf(processName);
            if (index !== -1) {
                this.configData.suppress_apps[packageName].processes.splice(index, 1);
                this.updateDisplay();
            }
        }
    },

    // 渲染页面
    render() {
        return `
            <div class="suppress-manager-container">
                <h2>${I18n.translate('SUPPRESS_MANAGER', '抑制管理器')}</h2>
                
                <!-- 已配置应用列表 -->
                <div class="configured-apps-section">
                    <div class="section-header">
                        <h3>${I18n.translate('CONFIGURED_APPS', '已配置应用')}</h3>
                        <span class="app-count">${Object.keys(this.configData.suppress_apps || {}).length}</span>
                                </div>
                    <div class="app-list configured-app-list" id="configured-apps">
                        ${this.renderConfiguredApps()}
                            </div>
                        </div>
                
                <!-- MD3风格浮动添加按钮 -->
                <button class="md3-fab" id="add-config-button" aria-label="${I18n.translate('ADD_APP', '添加应用')}">
                    <span class="material-symbols-rounded">add</span>
                </button>
                
                <!-- MD3风格对话框（初始隐藏） -->
                <dialog id="add-config-dialog" class="md3-dialog">
                    <!-- 弹窗内容将动态填充 -->
                </dialog>
                            
                <!-- MD3风格编辑对话框（初始隐藏） -->
                <dialog id="edit-dialog" class="md3-dialog">
                    <!-- 编辑对话框内容将动态填充 -->
                </dialog>
                    </div>
        `;
    },
    
    // 渲染已配置应用列表
    renderConfiguredApps() {
        const configuredApps = this.configData.suppress_apps || {};
        
        if (Object.keys(configuredApps).length === 0) {
            return `<div class="empty-state">${I18n.translate('NO_CONFIGURED_APPS', '没有已配置的应用')}</div>`;
        }
        
        let html = '';
        
        for (const [packageName, config] of Object.entries(configuredApps)) {
            // 如果有搜索查询，过滤结果
            if (this.searchQuery && !packageName.toLowerCase().includes(this.searchQuery.toLowerCase())) {
                continue;
            }
            
            const appInfo = this.installedApps.find(app => app.packageName === packageName) || {
                appName: packageName,
                packageName: packageName,
                versionName: '未知',
                iconPath: null
            };
            
            const isEnabled = config.enabled;
            const appIcon = appInfo.iconPath ? 
                `<img src="${this.escapeHtml(appInfo.iconPath)}" class="app-icon" alt="${this.escapeHtml(appInfo.appName)}">` : 
                `<div class="app-icon-placeholder"><span class="material-symbols-rounded">android</span></div>`;
            
            html += `
                <div class="app-card configured-app ${isEnabled ? 'enabled' : 'disabled'}">
                    ${appIcon}
                    <div class="app-info">
                        <div class="app-name">${this.escapeHtml(appInfo.appName)}</div>
                        <div class="package-name">${this.escapeHtml(packageName)}</div>
                        <div class="process-count">${I18n.translate('PROCESSES', '进程')}: ${config.processes.length}</div>
                    </div>
                    <div class="app-actions">
                        <label class="toggle-switch">
                            <input type="checkbox" class="toggle-app" data-package="${packageName}" ${isEnabled ? 'checked' : ''}>
                            <span></span>
                        </label>
                        <button class="md3-icon-button edit-app" data-package="${packageName}">
                            <span class="material-symbols-rounded">edit</span>
                        </button>
                        <button class="md3-icon-button delete-app" data-package="${packageName}">
                            <span class="material-symbols-rounded">delete</span>
                            </button>
                        </div>
                    </div>
            `;
        }
        
        return html || `<div class="empty-state">${I18n.translate('NO_MATCHING_APPS', '没有匹配的应用')}</div>`;
    },
    
    // 渲染可用应用列表
    renderAvailableApps() {
        const appListEl = document.getElementById('available-apps');
        if (!appListEl) return;
        
        // 显示加载中
        appListEl.innerHTML = '<div class="md3-loading-spinner"></div>';
        
        // 确保应用列表已加载
        if (this.installedApps.length === 0) {
            appListEl.innerHTML = `<div class="empty-state">${I18n.translate('NO_INSTALLED_APPS', '没有找到已安装的应用')}</div>`;
            return;
        }
        
        // 过滤已配置的和系统应用
        const configuredPackages = Object.keys(this.configData.suppress_apps || {});
        const filteredApps = this.installedApps.filter(app => {
            // 过滤掉已配置的应用
            const isConfigured = configuredPackages.includes(app.packageName);
            if (isConfigured) return false;
            
            // 根据设置过滤系统应用
            if (app.isSystem && !this.config.showSystemApps) return false;
            
            // 应用搜索过滤
            if (this.searchQuery) {
                const searchLower = this.searchQuery.toLowerCase();
                return app.packageName.toLowerCase().includes(searchLower) || 
                       app.appName.toLowerCase().includes(searchLower);
            }
            
            return true;
        });
    
    // 渲染应用列表
        if (filteredApps.length === 0) {
            appListEl.innerHTML = `<div class="empty-state">${I18n.translate('NO_MATCHING_APPS', '没有匹配的应用')}</div>`;
            return;
        }
        
        // 按非系统应用优先排序
        const sortedApps = filteredApps.sort((a, b) => {
            // 非系统应用排前面
            if (a.isSystem !== b.isSystem) {
                return a.isSystem ? 1 : -1;
            }
            // 同类型应用按名称排序
            return a.appName.localeCompare(b.appName);
        });
        
        let html = '';
        for (const app of sortedApps) {
            const appIcon = app.iconPath ? 
                `<img src="${this.escapeHtml(app.iconPath)}" class="app-icon" alt="${this.escapeHtml(app.appName)}">` : 
                `<div class="app-icon-placeholder"><span class="material-symbols-rounded">android</span></div>`;
            
            html += `
                <div class="app-card available-app ${app.isSystem ? 'system-app' : ''}">
                    ${appIcon}
                    <div class="app-info">
                        <div class="app-name">${this.escapeHtml(app.appName)}</div>
                        <div class="package-name">${this.escapeHtml(app.packageName)}</div>
                        <div class="app-version">${app.isSystem ? I18n.translate('SYSTEM_APP', '系统应用') : app.versionName}</div>
                            </div>
                    <div class="app-actions">
                        <button class="select-app-btn" data-package="${app.packageName}">
                            ${I18n.translate('SELECT', '选择')}
                    </button>
                        </div>
                    </div>
            `;
        }
        
        appListEl.innerHTML = html;
        
        // 绑定选择按钮事件
        const selectButtons = appListEl.querySelectorAll('.select-app-btn');
        selectButtons.forEach(btn => {
            btn.addEventListener('click', (e) => {
                const packageName = e.target.dataset.package;
                this.showProcessConfigDialog(packageName);
            });
        });
    },
    
    // 渲染编辑弹窗内容
    renderEditDialog(packageName) {
        const appConfig = this.configData.suppress_apps[packageName];
        const appInfo = this.installedApps.find(app => app.packageName === packageName) || {
            appName: packageName,
            packageName: packageName
        };
        
        return `
            <div class="app-info-header">
                <div class="app-name">${this.escapeHtml(appInfo.appName)}</div>
                <div class="package-name">${this.escapeHtml(packageName)}</div>
                </div>
                
            <div class="process-list-container">
                <div class="process-list-header">
                    <h4>${I18n.translate('PROCESSES_TO_SUPPRESS', '要抑制的进程')}</h4>
                            </div>
                            
                            <div class="process-list" id="process-list">
                    ${appConfig.processes.map((process, index) => `
                                <div class="process-item">
                            <div class="md3-input-field">
                                <input type="text" class="process-input" value="${this.escapeHtml(process)}" data-index="${index}">
                                    </div>
                            <button class="md3-icon-button remove-process" data-index="${index}">
                                <span class="material-symbols-rounded">remove</span>
                                    </button>
                                </div>
                    `).join('')}
                            </div>
                            
                <div class="process-actions" style="margin-top:16px;">
                    <button class="md3-button" id="add-process">
                        <span class="material-symbols-rounded">add</span>
                        ${I18n.translate('ADD_PROCESS', '添加进程')}
                                </button>
                </div>
            </div>
        `;
    },

    // 显示编辑弹窗
    showEditDialog(packageName) {
        this.editingApp = packageName;
        
        const dialogEl = document.getElementById('edit-dialog');
        
        if (dialogEl) {
            dialogEl.innerHTML = `
                <div class="md3-dialog-content">
                    <div class="md3-dialog-header">
                        <h3>${I18n.translate('EDIT_APP_CONFIG', '编辑应用配置')}</h3>
                        <button class="md3-icon-button" id="close-edit-dialog">
                            <span class="material-symbols-rounded">close</span>
                                </button>
                            </div>
                    
                    <div class="md3-dialog-body" id="edit-dialog-body">
                        ${this.renderEditDialog(packageName)}
                        </div>
                    
                    <div class="md3-dialog-footer">
                        <button class="md3-button" id="cancel-edit">${I18n.translate('CANCEL', '取消')}</button>
                        <button class="md3-button filled" id="save-edit">${I18n.translate('SAVE', '保存')}</button>
                    </div>
                </div>
            `;
            
            dialogEl.showModal();
            
            // 绑定添加进程按钮
            const addProcessBtn = document.getElementById('add-process');
            if (addProcessBtn) {
                addProcessBtn.addEventListener('click', () => {
                    const processList = document.getElementById('process-list');
                    const newIndex = this.configData.suppress_apps[packageName].processes.length;
                    const defaultProcess = `${packageName}:process${newIndex}`;
                    
                    const newProcessItem = document.createElement('div');
                    newProcessItem.className = 'process-item';
                    newProcessItem.innerHTML = `
                        <div class="md3-input-field">
                            <input type="text" class="process-input" value="${defaultProcess}" data-index="${newIndex}">
            </div>
                        <button class="md3-icon-button remove-process" data-index="${newIndex}">
                            <span class="material-symbols-rounded">remove</span>
                        </button>
                    `;
                    
                    processList.appendChild(newProcessItem);
                    
                    // 绑定新添加的移除按钮
                    const removeBtn = newProcessItem.querySelector('.remove-process');
                    if (removeBtn) {
                        removeBtn.addEventListener('click', (e) => {
                            e.target.closest('.process-item').remove();
                        });
                    }
                });
            }
            
            // 绑定移除进程按钮
            const removeProcessBtns = document.querySelectorAll('.remove-process');
            removeProcessBtns.forEach(btn => {
                btn.addEventListener('click', (e) => {
                    e.target.closest('.process-item').remove();
                });
            });
            
            // 绑定关闭按钮
            const closeBtn = document.getElementById('close-edit-dialog');
            if (closeBtn) {
                closeBtn.addEventListener('click', () => this.hideEditDialog());
            }
            
            // 绑定取消按钮
            const cancelEditBtn = document.getElementById('cancel-edit');
            if (cancelEditBtn) {
                cancelEditBtn.addEventListener('click', () => this.hideEditDialog());
            }
            
            // 绑定保存按钮
            const saveEditBtn = document.getElementById('save-edit');
            if (saveEditBtn) {
                saveEditBtn.addEventListener('click', () => this.saveEditDialog());
            }
            
            // 添加点击外部关闭对话框
            dialogEl.addEventListener('click', (e) => {
                if (e.target === dialogEl) {
                    this.hideEditDialog();
                }
            });
        }
    },
    
    // 隐藏编辑弹窗
    hideEditDialog() {
        const dialogEl = document.getElementById('edit-dialog');
        if (dialogEl) {
            dialogEl.close();
            this.editingApp = null;
        }
    },
    
    // 保存编辑内容
    saveEditDialog() {
        if (!this.editingApp) return;
        
        const processInputs = document.querySelectorAll('.process-input');
        const processes = Array.from(processInputs).map(input => input.value.trim()).filter(Boolean);
        
        if (processes.length === 0) {
            Core.showToast(I18n.translate('PROCESS_REQUIRED', '至少需要一个进程'), 'warning');
            return;
        }
        
        // 更新配置
        this.configData.suppress_apps[this.editingApp].processes = processes;
        
        // 隐藏弹窗并更新显示
        this.hideEditDialog();
        this.updateDisplay();
        
        Core.showToast(I18n.translate('APP_CONFIG_UPDATED', '应用配置已更新'), 'success');
    },
    
    // 渲染后回调
    afterRender() {
        this.bindEvents();
    },
    
    // 绑定事件
    bindEvents() {
        // 浮动添加按钮
        const addConfigButton = document.getElementById('add-config-button');
        if (addConfigButton) {
            addConfigButton.addEventListener('click', () => this.showAddConfigDialog());
        }
        
        // 配置应用开关切换
        const toggleButtons = document.querySelectorAll('.toggle-app');
        toggleButtons.forEach(toggle => {
            toggle.addEventListener('change', (e) => {
                const packageName = e.target.dataset.package;
                this.toggleAppEnabled(packageName);
            });
        });
        
        // 编辑应用按钮
        const editButtons = document.querySelectorAll('.edit-app');
        editButtons.forEach(btn => {
            btn.addEventListener('click', (e) => {
                const packageName = e.target.closest('.edit-app').dataset.package;
                this.showEditDialog(packageName);
            });
        });
        
        // 删除应用按钮
        const deleteButtons = document.querySelectorAll('.delete-app');
        deleteButtons.forEach(btn => {
            btn.addEventListener('click', (e) => {
                const packageName = e.target.closest('.delete-app').dataset.package;
                this.removeAppConfig(packageName);
            });
        });
        
        // 编辑弹窗关闭按钮
        const closeDialogBtn = document.getElementById('close-edit-dialog');
        if (closeDialogBtn) {
            closeDialogBtn.addEventListener('click', () => this.hideEditDialog());
        }
        
        // 编辑弹窗取消按钮
        const cancelEditBtn = document.getElementById('cancel-edit');
        if (cancelEditBtn) {
            cancelEditBtn.addEventListener('click', () => this.hideEditDialog());
        }
        
        // 编辑弹窗保存按钮
        const saveEditBtn = document.getElementById('save-edit');
        if (saveEditBtn) {
            saveEditBtn.addEventListener('click', () => this.saveEditDialog());
        }
    },

    // 注册操作按钮
    registerActions() {
        UI.registerPageActions('suppress_manager', [
            {
                id: 'refresh-config',
                icon: 'refresh',
                title: I18n.translate('REFRESH', '刷新'),
                onClick: this.refreshConfig.bind(this)
            },
            {
                id: 'restore-config',
                icon: 'restore',
                title: I18n.translate('RESTORE_CONFIG', '恢复设置'),
                onClick: this.restoreConfig.bind(this)
            },
            {
                id: 'save-config',
                icon: 'save',
                title: I18n.translate('SAVE', '保存'),
                onClick: this.saveConfig.bind(this)
            },
            {
                id: 'help-button',
                icon: 'help',
                title: I18n.translate('HELP', '帮助'),
                onClick: this.showHelp.bind(this)
            }
        ]);
    },

    // 刷新配置
    async refreshConfig() {
        try {
            // 如果有未保存的修改，提示用户
            if (this.hasUnsavedChanges()) {
                const confirmRefresh = confirm(I18n.translate('CONFIRM_REFRESH', '有未保存的更改，确定要刷新吗？'));
                if (!confirmRefresh) return;
            }
            
            this.showLoading();
            const [configData, installedApps] = await Promise.all([
                this.loadConfig(),
                this.loadInstalledApps()
            ]);
            
            this.configData = configData;
            this.installedApps = installedApps;
            
            this.updateDisplay();
            Core.showToast(I18n.translate('CONFIG_REFRESHED', '配置已刷新'), 'success');
        } catch (error) {
            console.error('刷新配置失败:', error);
            Core.showToast(I18n.translate('CONFIG_REFRESH_ERROR', '刷新配置失败'), 'error');
        } finally {
            this.hideLoading();
        }
    },

    // 检查是否有未保存的更改
    hasUnsavedChanges() {
        // 这里可以实现对比原始配置和当前配置的逻辑
        // 简化版本直接返回false，后续可以扩展完善
        return false;
    },

    // 更新显示
    updateDisplay() {
        const configuredAppsEl = document.getElementById('configured-apps');
        const appCountEls = document.querySelectorAll('.app-count');
        
        if (configuredAppsEl) {
            configuredAppsEl.innerHTML = this.renderConfiguredApps();
        }
        
        // 如果弹窗显示中，更新可用应用列表
        if (document.getElementById('add-config-dialog') && 
            document.getElementById('add-config-dialog').style.display === 'flex' &&
            document.getElementById('available-apps')) {
            this.loadAvailableApps();
        }
        
        if (appCountEls.length >= 1) {
            appCountEls[0].textContent = Object.keys(this.configData.suppress_apps || {}).length;
        }
        
        // 重新绑定事件
        this.bindEvents();
    },

    // 显示加载中
    showLoading() {
        this.isLoading = true;
        
        // 创建加载遮罩
        const loadingOverlay = document.createElement('div');
        loadingOverlay.className = 'loading-overlay';
        loadingOverlay.innerHTML = '<div class="loading-spinner"></div>';
        loadingOverlay.style.display = 'flex';
        loadingOverlay.style.opacity = '1';
        loadingOverlay.style.visibility = 'visible';
        
        document.querySelector('.suppress-manager-container').appendChild(loadingOverlay);
    },

    // 隐藏加载中
    hideLoading() {
        this.isLoading = false;
        const loadingElement = document.querySelector('.loading-overlay');
        if (loadingElement) {
            loadingElement.remove();
        }
    },

    // HTML转义
    escapeHtml(unsafe) {
        return unsafe
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
            .replace(/>/g, "&gt;")
            .replace(/"/g, "&quot;")
            .replace(/'/g, "&#039;");
    },

    // 语言切换处理
    onLanguageChanged() {
        this.updateDisplay();
    },

    // 激活页面
    onActivate() {
        // 如果有临时配置状态且有未保存的更改，恢复临时状态
        if (this.tempConfigState && Object.keys(this.tempConfigState).length > 0 && this.hasUnsavedChanges) {
            this.configData = JSON.parse(JSON.stringify(this.tempConfigState));
            this.updateDisplay();
            Core.showToast(I18n.translate('UNSAVED_CHANGES_RESTORED', '已恢复未保存的更改'), 'info');
        }
    },

    // 停用页面
    onDeactivate() {
        // 检查是否有未保存的更改
        this.hasUnsavedChanges = this.checkForUnsavedChanges();
        if (this.hasUnsavedChanges) {
            // 保存临时配置状态
            this.saveTemporaryConfigState();
            Core.showToast(I18n.translate('UNSAVED_CHANGES', '有未保存的更改'), 'warning');
        }
        
        // 注销语言切换处理器
        I18n.unregisterLanguageChangeHandler(this.onLanguageChanged.bind(this));
        
        // 清理页面操作按钮
        UI.clearPageActions('suppress_manager');
    },

    // 显示添加配置弹窗
    showAddConfigDialog() {
        const dialogEl = document.getElementById('add-config-dialog');
        if (!dialogEl) return;
        
        // 渲染弹窗内容
        dialogEl.innerHTML = `
            <div class="md3-dialog-content">
                <div class="md3-dialog-header">
                    <h3>${I18n.translate('ADD_APP_CONFIG', '添加应用配置')}</h3>
                    <button class="md3-icon-button" id="close-add-dialog">
                        <span class="material-symbols-rounded">close</span>
                    </button>
                </div>
                
                <div class="md3-dialog-body">
                    <!-- 搜索和过滤选项 -->
                    <div class="search-container">
                        <label class="md3-search-field">
                            <span class="material-symbols-rounded">search</span>
                            <input type="text" id="app-search" placeholder="${I18n.translate('SEARCH_APPS', '搜索应用')}" value="${this.searchQuery}">
                        </label>
                        
                        <div class="filter-options">
                            <label class="md3-checkbox">
                                <input type="checkbox" id="show-system-apps" ${this.config.showSystemApps ? 'checked' : ''}>
                                <span>${I18n.translate('SHOW_SYSTEM_APPS', '显示系统应用')}</span>
                            </label>
                        </div>
                    </div>
                    
                    <!-- 应用列表 -->
                    <div class="app-list available-app-list" id="available-apps">
                        <div class="md3-loading-spinner"></div>
                    </div>
                </div>
            </div>
        `;
        
        // 显示弹窗
        dialogEl.showModal();
        
        // 绑定关闭按钮
        const closeBtn = document.getElementById('close-add-dialog');
        if (closeBtn) {
            closeBtn.addEventListener('click', () => this.hideAddConfigDialog());
        }
        
        // 绑定搜索框
        const searchInput = document.getElementById('app-search');
        if (searchInput) {
            searchInput.addEventListener('input', (e) => {
                this.searchQuery = e.target.value;
                this.loadAvailableApps();
            });
            // 聚焦搜索框
            searchInput.focus();
        }
        
        // 绑定系统应用显示开关
        const showSystemAppsCheckbox = document.getElementById('show-system-apps');
        if (showSystemAppsCheckbox) {
            showSystemAppsCheckbox.addEventListener('change', (e) => {
                this.config.showSystemApps = e.target.checked;
                this.loadAvailableApps();
            });
        }
        
        // 添加点击外部关闭对话框
        dialogEl.addEventListener('click', (e) => {
            if (e.target === dialogEl) {
                this.hideAddConfigDialog();
            }
        });
        
        // 加载可用应用列表
        this.loadAvailableApps();
    },
    
    // 隐藏添加配置弹窗
    hideAddConfigDialog() {
        const dialogEl = document.getElementById('add-config-dialog');
        if (dialogEl) {
            dialogEl.close();
            this.searchQuery = ''; // 重置搜索查询
        }
    },
    
    // 加载可用应用列表
    async loadAvailableApps() {
        this.renderAvailableApps();
    },

    // 显示进程配置弹窗
    showProcessConfigDialog(packageName) {
        // 隐藏应用选择弹窗
        this.hideAddConfigDialog();
        
        const dialogEl = document.getElementById('add-config-dialog');
        if (!dialogEl) return;
        
        // 获取应用信息
        const appInfo = this.installedApps.find(app => app.packageName === packageName) || {
            appName: packageName,
            packageName: packageName
        };
        
        // 渲染弹窗内容
        dialogEl.innerHTML = `
            <div class="md3-dialog-content">
                <div class="md3-dialog-header">
                    <h3>${I18n.translate('CONFIGURE_PROCESSES', '配置进程')}</h3>
                    <button class="md3-icon-button" id="close-process-dialog">
                        <span class="material-symbols-rounded">close</span>
            </button>
                </div>
                
                <div class="md3-dialog-body">
                    <div class="app-info-header">
                        <div class="app-name">${this.escapeHtml(appInfo.appName)}</div>
                        <div class="package-name">${this.escapeHtml(packageName)}</div>
                    </div>
                    
                    <div class="process-config-section">
                        <h4>${I18n.translate('PROCESSES_TO_SUPPRESS', '要抑制的进程')}</h4>
                        
                        <div class="process-list" id="process-list">
                            <div class="process-item">
                                <div class="md3-input-field">
                                    <input type="text" class="process-input" value="${packageName}:appbrand0" data-index="0">
                                </div>
                                <button class="md3-icon-button remove-process" data-index="0" style="visibility:hidden">
                                    <span class="material-symbols-rounded">remove</span>
                                </button>
                            </div>
                        </div>
                        
                        <div class="process-actions">
                            <button class="md3-button" id="add-process-btn">
                                <span class="material-symbols-rounded">add</span>
                                ${I18n.translate('ADD_PROCESS', '添加进程')}
                            </button>
                        </div>
                    </div>
                </div>
                
                <div class="md3-dialog-footer">
                    <button class="md3-button" id="back-to-apps">
                        ${I18n.translate('BACK', '返回')}
                    </button>
                    <button class="md3-button filled" id="save-process-config">
                        ${I18n.translate('SAVE', '保存')}
                    </button>
                </div>
            </div>
        `;
        
        // 显示弹窗
        dialogEl.showModal();
        
        // 绑定事件
        this.bindProcessConfigEvents(packageName);
    },
    
    // 绑定进程配置弹窗事件
    bindProcessConfigEvents(packageName) {
        // 关闭按钮
        const closeBtn = document.getElementById('close-process-dialog');
        if (closeBtn) {
            closeBtn.addEventListener('click', () => this.hideAddConfigDialog());
        }
        
        // 返回按钮
        const backBtn = document.getElementById('back-to-apps');
        if (backBtn) {
            backBtn.addEventListener('click', () => this.showAddConfigDialog());
        }
        
        // 添加进程按钮
        const addProcessBtn = document.getElementById('add-process-btn');
        if (addProcessBtn) {
            addProcessBtn.addEventListener('click', () => this.addProcessInput(packageName));
        }
        
        // 保存按钮
        const saveBtn = document.getElementById('save-process-config');
        if (saveBtn) {
            saveBtn.addEventListener('click', () => this.saveProcessConfig(packageName));
        }
    },
    
    // 添加进程输入框
    addProcessInput(packageName) {
        const processList = document.getElementById('process-list');
        if (!processList) return;
        
        const processItems = processList.querySelectorAll('.process-item');
        const newIndex = processItems.length;
        
        // 创建新的进程输入项
        const newProcessItem = document.createElement('div');
        newProcessItem.className = 'process-item';
        newProcessItem.innerHTML = `
            <div class="md3-input-field">
                <input type="text" class="process-input" value="${packageName}:process${newIndex}" data-index="${newIndex}">
            </div>
            <button class="md3-icon-button remove-process" data-index="${newIndex}">
                <span class="material-symbols-rounded">remove</span>
            </button>
        `;
        
        processList.appendChild(newProcessItem);
        
        // 绑定移除按钮事件
        const removeBtn = newProcessItem.querySelector('.remove-process');
        if (removeBtn) {
            removeBtn.addEventListener('click', () => {
                newProcessItem.remove();
            });
        }
        
        // 聚焦新的输入框
        const input = newProcessItem.querySelector('.process-input');
        if (input) {
            input.focus();
            input.select();
        }
    },
    
    // 保存进程配置
    saveProcessConfig(packageName) {
        // 获取所有进程输入框的值
        const processInputs = document.querySelectorAll('.process-input');
        if (!processInputs.length) return;
        
        const processes = Array.from(processInputs).map(input => input.value.trim()).filter(Boolean);
        
        if (processes.length === 0) {
            Core.showToast(I18n.translate('PROCESS_REQUIRED', '至少需要一个进程'), 'warning');
            return;
        }
        
        // 添加应用配置
        this.addAppConfig(packageName, processes);
        
        // 隐藏弹窗
        this.hideAddConfigDialog();
        
        // 显示成功提示
        Core.showToast(I18n.translate('CONFIG_ADDED', '配置已添加'), 'success');
    },

    // 帮助提示
    showHelp() {
        Core.showToast(I18n.translate('SUPPRESS_HELP', '抑制管理器用于配置应用进程抑制。点击右下角按钮添加新配置。'), 'info', 5000);
    },

    // 创建配置备份
    createConfigBackup() {
        this.configBackup = JSON.parse(JSON.stringify(this.configData));
    },

    // 恢复初始配置
    async restoreConfig() {
        try {
            if (!this.configBackup || Object.keys(this.configBackup).length === 0) {
                Core.showToast(I18n.translate('NO_CONFIG_BACKUP', '没有可用的配置备份'), 'error');
            return;
        }
        
            const confirmRestore = confirm(I18n.translate('CONFIRM_RESTORE', '确定要恢复初始设置吗？'));
            if (!confirmRestore) return;

            // 恢复设置
            this.configData = JSON.parse(JSON.stringify(this.configBackup));

            // 更新显示
            this.updateDisplay();

            // 重置未保存更改标志
            this.hasUnsavedChanges = false;

            // 显示成功消息
            Core.showToast(I18n.translate('CONFIG_RESTORED', '设置已恢复'), 'success');
        } catch (error) {
            console.error('恢复设置失败:', error);
            Core.showToast(I18n.translate('CONFIG_RESTORE_ERROR', '恢复设置失败'), 'error');
        }
    },

    // 检查是否有未保存的更改
    checkForUnsavedChanges() {
        // 如果没有备份，则无法比较
        if (!this.configBackup || Object.keys(this.configBackup).length === 0) {
            return false;
        }
        
        // 比较当前配置和备份
        return JSON.stringify(this.configData) !== JSON.stringify(this.configBackup);
    },
    
    // 保存临时配置状态
    saveTemporaryConfigState() {
        if (!this.hasUnsavedChanges) return;
        this.tempConfigState = JSON.parse(JSON.stringify(this.configData));
    }
};

// 注册预加载
PreloadManager.registerDataLoader('suppress_manager', SuppressManagerPage.preloadData.bind(SuppressManagerPage));

// 导出模块
window.SuppressManagerPage = SuppressManagerPage;