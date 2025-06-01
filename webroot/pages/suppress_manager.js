/**
 * AMMF WebUI 抑制管理器页面模块
 * 管理DeepSuppressor应用抑制配置
 */

const SuppressManagerPage = {
    // 状态和数据
    configData: { suppress_apps: {} },
    configBackup: null,
    installedApps: [],
    isLoading: false,
    searchQuery: '',
    selectedApp: null,
    hasUnsavedChanges: false,

    // 配置项
    config: {
        configPath: 'module_settings/suppress_config.json',
        showSystemApps: false
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
            return { configData: { suppress_apps: {} }, installedApps: [] };
        }
    },

    // 初始化
    async init() {
        try {
            // 重置取消标志
            this.isCancelled = false;

            // 显示加载状态但不阻塞UI
            this.setLoadingState(true);

            // 获取预加载的数据或重新加载
            const preloadedData = PreloadManager.getData('suppress_manager');

            if (preloadedData) {
                this.configData = preloadedData.configData || { suppress_apps: {} };
                this.installedApps = preloadedData.installedApps || [];
            } else {
                // 异步加载数据
                await Promise.all([
                    this.loadConfig(),
                    this.loadInstalledApps()
                ]);
            }

            // 创建配置备份
            this.createConfigBackup();

            // 注册操作按钮和语言切换处理器
            this.registerActions();
            I18n.registerLanguageChangeHandler(this.onLanguageChanged.bind(this));

            this.setLoadingState(false);
            return true;
        } catch (error) {
            console.error('初始化抑制管理器页面失败:', error);
            this.setLoadingState(false);
            Core.showToast(I18n.translate('INIT_FAILED', '初始化失败'), 'error');
            return false;
        }
    },

    // 加载配置
    async loadConfig() {
        try {
            // 使用 fetch API 代替命令行读取
            const response = await fetch(`/api/file?path=${encodeURIComponent(this.config.configPath)}`);

            if (!response.ok) {
                throw new Error(`加载配置失败: ${response.statusText}`);
            }

            const configData = await response.json();
            this.configData = configData || { suppress_apps: {} };

            return this.configData;
        } catch (error) {
            console.error('加载配置失败:', error);
            // 使用空配置
            this.configData = { suppress_apps: {} };
            return this.configData;
        }
    },
    async loadInstalledApps() {
        try {
            // 批量获取应用列表
            const [appListCmd, systemAppsCmd] = await Promise.all([
                Core.execCommand('pm list packages -3'),
                Core.execCommand('pm list packages -s')
            ]);

            // 解析应用列表
            const thirdPartyApps = this.parsePackageList(appListCmd);
            const systemApps = this.parsePackageList(systemAppsCmd);
            const allApps = [...thirdPartyApps, ...systemApps].sort();

            // 分批获取应用信息，避免一次性请求过多
            const batchSize = 10;
            const appsWithInfo = [];

            for (let i = 0; i < allApps.length; i += batchSize) {
                const batch = allApps.slice(i, i + batchSize);
                const batchPromises = batch.map(packageName => this.getAppInfo(packageName, systemApps));
                const batchResults = await Promise.all(batchPromises);
                appsWithInfo.push(...batchResults);

                // 每批次处理完更新一次UI
                if (this.isVisible) {
                    this.installedApps = appsWithInfo;
                    this.updateAppList();
                }
            }

            this.installedApps = appsWithInfo;
            return appsWithInfo;
        } catch (error) {
            console.error('加载已安装应用失败:', error);
            return [];
        }
    },

    // 解析包列表
    parsePackageList(output) {
        return output.split('\n')
            .filter(line => line.trim().startsWith('package:'))
            .map(line => line.trim().substring(8));
    },

    // 获取单个应用信息
    async getAppInfo(packageName, systemApps) {
        try {
            const appInfoCmd = await Core.execCommand(
                `dumpsys package ${packageName} | grep -E "labelRes|versionName|applicationInfo"`
            );

            const nameMatch = appInfoCmd.match(/labelRes=\d+ nonLocalizedLabel=([^ ]+)/);
            const versionMatch = appInfoCmd.match(/versionName=([^ ]+)/);

            return {
                packageName,
                appName: nameMatch ? nameMatch[1] : packageName,
                versionName: versionMatch ? versionMatch[1] : I18n.translate('UNKNOWN', '未知'),
                isSystem: systemApps.includes(packageName),
                iconPath: null // 暂不加载图标，可以后续实现懒加载
            };
        } catch (error) {
            return this.createDefaultAppInfo(packageName, systemApps);
        }
    },

    // 创建默认应用信息
    createDefaultAppInfo(packageName, systemApps) {
        return {
            packageName,
            appName: packageName,
            versionName: I18n.translate('UNKNOWN', '未知'),
            isSystem: systemApps.includes(packageName),
            iconPath: null
        };
    },

    // 创建配置备份
    createConfigBackup() {
        this.configBackup = JSON.parse(JSON.stringify(this.configData));
        this.hasUnsavedChanges = false;
    },

    // 保存配置
    async saveConfig() {
        try {
            this.setLoadingState(true);

            // 验证配置
            if (!this.validateConfig()) {
                Core.showToast(I18n.translate('CONFIG_INVALID', '配置无效，请检查'), 'error');
                this.setLoadingState(false);
                return false;
            }

            // 格式化JSON
            const configJson = JSON.stringify(this.configData, null, 2);

            try {
                // 备份当前配置
                await Core.execCommand(`cp ${this.config.configPath} ${this.config.configPath}.bak`);

                // 写入新配置
                await Core.execCommand(`echo '${configJson.replace(/'/g, "'\\''")}' > ${this.config.configPath}`);

                // 重启服务以应用配置
                await Core.execCommand('sh ${Core.MODULE_PATH}/service.sh restart');
            } catch (error) {
                console.error('保存配置文件失败:', error);
                Core.showToast(I18n.translate('CONFIG_SAVE_ERROR', '保存配置失败'), 'error');
                return false;
            }

            // 更新备份
            this.createConfigBackup();

            Core.showToast(I18n.translate('CONFIG_SAVED', '配置已保存并应用'), 'success');
            return true;
        } catch (error) {
            console.error('保存配置失败:', error);
            Core.showToast(I18n.translate('CONFIG_SAVE_ERROR', '保存配置失败'), 'error');
            return false;
        } finally {
            this.setLoadingState(false);
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

            // 必须有enabled属性
            if (typeof config.enabled !== 'boolean') {
                return false;
            }

            // 必须有进程列表
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

        this.hasUnsavedChanges = true;
        this.updateUI();
        Core.showToast(I18n.translate('APP_CONFIG_ADDED', '应用配置已添加'), 'success');
    },

    // 移除应用配置
    removeAppConfig(packageName) {
        if (this.configData.suppress_apps && this.configData.suppress_apps[packageName]) {
            delete this.configData.suppress_apps[packageName];
            this.hasUnsavedChanges = true;
            this.updateUI();
            Core.showToast(I18n.translate('APP_CONFIG_REMOVED', '应用配置已移除'), 'success');
        }
    },

    // 切换应用启用状态
    toggleAppEnabled(packageName) {
        if (this.configData.suppress_apps && this.configData.suppress_apps[packageName]) {
            this.configData.suppress_apps[packageName].enabled =
                !this.configData.suppress_apps[packageName].enabled;
            this.hasUnsavedChanges = true;
            this.updateUI();
            const status = this.configData.suppress_apps[packageName].enabled;
            Core.showToast(
                status 
                    ? I18n.translate('APP_ENABLED', '应用已启用')
                    : I18n.translate('APP_DISABLED', '应用已禁用'),
                'success'
            );
        }
    },

    // 添加进程到应用配置
    addProcess(packageName, processName) {
        if (this.configData.suppress_apps && this.configData.suppress_apps[packageName]) {
            if (!this.configData.suppress_apps[packageName].processes.includes(processName)) {
                this.configData.suppress_apps[packageName].processes.push(processName);
                this.updateUI();
            }
        }
    },

    // 从应用配置中移除进程
    removeProcess(packageName, processName) {
        if (this.configData.suppress_apps && this.configData.suppress_apps[packageName]) {
            const index = this.configData.suppress_apps[packageName].processes.indexOf(processName);
            if (index !== -1) {
                this.configData.suppress_apps[packageName].processes.splice(index, 1);
                this.updateUI();
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
                        <div class="loading-placeholder">
                            ${I18n.translate('LOADING_APPS', '正在加载应用...')}
                        </div>
                            </div>
                        </div>
                
                <!-- MD3风格浮动添加按钮 -->
                <button class="md3-fab" id="add-config-button" aria-label="${I18n.translate('ADD_APP', '添加应用')}">
                    <span class="material-symbols-rounded">add</span>
                </button>
                
                <!-- MD3风格对话框（初始隐藏） -->
                <dialog id="app-dialog" class="md3-dialog">
                    <!-- 弹窗内容将动态填充 -->
                </dialog>
                            
                <div id="settings-loading" class="loading-overlay">
                    <div class="loading-spinner"></div>
                </div>
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
            const appInfo = this.installedApps.find(app => app.packageName === packageName) || {
                appName: packageName,
                packageName: packageName,
                versionName: I18n.translate('UNKNOWN', '未知'),
                iconPath: null
            };

            const isEnabled = config.enabled;
            const appIcon = appInfo.iconPath
                ? `<img src="${this.escapeHtml(appInfo.iconPath)}" class="app-icon" alt="${this.escapeHtml(appInfo.appName)}">` 
                : `<div class="app-icon-placeholder"><span class="material-symbols-rounded">android</span></div>`;

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

        return html;
    },

    // 渲染可用应用列表
    renderAvailableApps() {
        // 过滤已配置的和系统应用
        const configuredPackages = Object.keys(this.configData.suppress_apps || {});
        const filteredApps = this.installedApps.filter(app => {
            // 过滤掉已配置的应用
            const isConfigured = configuredPackages.includes(app.packageName);
            if (isConfigured) return false;
            return true;
        });

        if (filteredApps.length === 0) {
            return `<div class="empty-state">${I18n.translate('NO_MATCHING_APPS', '没有匹配的应用')}</div>`;
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
                <div class="app-card available-app ${app.isSystem ? 'system-app' : ''}" data-package="${app.packageName}">
                    ${appIcon}
                    <div class="app-info">
                        <div class="app-name">${this.escapeHtml(app.appName)}</div>
                        <div class="package-name">${this.escapeHtml(app.packageName)}</div>
                        <div class="app-version">${app.isSystem ? I18n.translate('SYSTEM_APP', '系统应用') : app.versionName}</div>
                            </div>
                </div>
            `;
        }

        return html;
    },

    // 显示应用选择对话框
    showAppSelectionDialog() {
        const dialogEl = document.getElementById('app-dialog');
        if (!dialogEl) return;

        // 渲染弹窗内容
        dialogEl.innerHTML = `
            <div class="md3-dialog-content">
                <div class="md3-dialog-header">
                    <h3>${I18n.translate('SELECT_APP', '选择应用')}</h3>
                    <button class="md3-icon-button" id="close-dialog">
                        <span class="material-symbols-rounded">close</span>
                    </button>
                        </div>
                
                <div class="md3-dialog-body">
                    <div class="search-container">
                        <div class="md3-search-field">
                            <span class="material-symbols-rounded">search</span>
                            <input type="text" id="dialog-app-search" placeholder="${I18n.translate('SEARCH_APPS', '搜索应用')}" value="${this.searchQuery}">
                        </div>
                        
                        <div class="filter-options">
                            <label class="md3-checkbox">
                                <input type="checkbox" id="dialog-show-system-apps" ${this.config.showSystemApps ? 'checked' : ''}>
                                <span>${I18n.translate('SHOW_SYSTEM_APPS', '显示系统应用')}</span>
                            </label>
                        </div>
                    </div>
                    
                    <div class="app-list available-app-list" id="available-apps">
                        <div class="loading-placeholder">
                            ${I18n.translate('LOADING_APPS', '正在加载应用...')}
                        </div>
                    </div>
                        </div>
                    </div>
            `;

        dialogEl.showModal();

        // 渲染应用列表
        const appListEl = dialogEl.querySelector('#available-apps');
        if (appListEl) {
            appListEl.innerHTML = this.renderAvailableApps();
        }

        // 绑定事件
        this.bindDialogEvents(dialogEl);
    },

    // 显示编辑对话框
    showEditDialog(packageName) {
        const dialogEl = document.getElementById('app-dialog');
        if (!dialogEl) return;

        const appConfig = this.configData.suppress_apps[packageName];
        const appInfo = this.installedApps.find(app => app.packageName === packageName) || {
            appName: packageName,
            packageName: packageName
        };

        dialogEl.innerHTML = `
            <div class="md3-dialog-content">
                <div class="md3-dialog-header">
                    <h3>${I18n.translate('EDIT_APP_CONFIG', '编辑应用配置')}</h3>
                    <button class="md3-icon-button" id="close-dialog">
                        <span class="material-symbols-rounded">close</span>
                    </button>
                </div>
                
                <div class="md3-dialog-body">
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
                                    <button class="md3-icon-button remove-process" data-index="${index}" ${appConfig.processes.length <= 1 ? 'disabled' : ''}>
                                <span class="material-symbols-rounded">remove</span>
                                    </button>
                                </div>
                    `).join('')}
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
                    <button class="md3-button" id="cancel-edit">${I18n.translate('CANCEL', '取消')}</button>
                    <button class="md3-button filled" id="save-edit">${I18n.translate('SAVE', '保存')}</button>
                </div>
            </div>
        `;

        dialogEl.showModal();
        this.selectedApp = packageName;

        // 绑定事件
        this.bindEditDialogEvents(dialogEl, packageName);
    },

    // 显示进程配置对话框
    showProcessConfigDialog(packageName) {
        const dialogEl = document.getElementById('app-dialog');
        if (!dialogEl) return;

        const appInfo = this.installedApps.find(app => app.packageName === packageName) || {
            appName: packageName,
            packageName: packageName
        };

        dialogEl.innerHTML = `
                <div class="md3-dialog-content">
                    <div class="md3-dialog-header">
                    <h3>${I18n.translate('CONFIGURE_PROCESSES', '配置进程')}</h3>
                    <button class="md3-icon-button" id="close-dialog">
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
                                <button class="md3-icon-button remove-process" data-index="0" disabled>
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
                    <button class="md3-button" id="cancel-config">${I18n.translate('CANCEL', '取消')}</button>
                    <button class="md3-button filled" id="save-config-btn">${I18n.translate('SAVE', '保存')}</button>
                    </div>
                </div>
            `;

        dialogEl.showModal();
        this.selectedApp = packageName;

        // 绑定事件
        this.bindProcessConfigEvents(dialogEl, packageName);
    },

    // 绑定对话框事件
    bindDialogEvents(dialogEl) {
        // 关闭按钮
        const closeBtn = dialogEl.querySelector('#close-dialog');
        if (closeBtn) {
            closeBtn.addEventListener('click', () => {
                dialogEl.close();
                this.selectedApp = null;
            });
        }

        // 搜索框
        const searchInput = dialogEl.querySelector('#dialog-app-search');
        if (searchInput) {
            searchInput.addEventListener('input', (e) => {
                this.searchQuery = e.target.value;
                const appListEl = dialogEl.querySelector('#available-apps');
                if (appListEl) {
                    appListEl.innerHTML = this.renderAvailableApps();
                    this.bindAppSelectionEvents(appListEl);
                }
            });

            // 自动聚焦搜索框
            setTimeout(() => searchInput.focus(), 100);
        }

        // 系统应用开关
        const systemAppsSwitch = dialogEl.querySelector('#dialog-show-system-apps');
        if (systemAppsSwitch) {
            systemAppsSwitch.addEventListener('change', (e) => {
                this.config.showSystemApps = e.target.checked;
                const appListEl = dialogEl.querySelector('#available-apps');
                if (appListEl) {
                    appListEl.innerHTML = this.renderAvailableApps();
                    this.bindAppSelectionEvents(appListEl);
                }
            });
        }

        // 应用点击事件
        const appListEl = dialogEl.querySelector('#available-apps');
        if (appListEl) {
            this.bindAppSelectionEvents(appListEl);
        }

        // 点击对话框外部关闭
        dialogEl.addEventListener('click', (e) => {
            if (e.target === dialogEl) {
                dialogEl.close();
                this.selectedApp = null;
            }
        });
    },

    // 绑定应用选择事件
    bindAppSelectionEvents(appListEl) {
        const appCards = appListEl.querySelectorAll('.app-card');
        appCards.forEach(card => {
            card.addEventListener('click', () => {
                const packageName = card.dataset.package;
                this.showProcessConfigDialog(packageName);
            });
        });
    },

    // 绑定编辑对话框事件
    bindEditDialogEvents(dialogEl, packageName) {
        // 关闭按钮
        const closeBtn = dialogEl.querySelector('#close-dialog');
        if (closeBtn) {
            closeBtn.addEventListener('click', () => {
                dialogEl.close();
                this.selectedApp = null;
            });
        }

        // 添加进程按钮
        const addProcessBtn = dialogEl.querySelector('#add-process-btn');
        if (addProcessBtn) {
            addProcessBtn.addEventListener('click', () => this.addProcessInput(packageName));
        }

        // 删除进程按钮
        const removeProcessBtns = dialogEl.querySelectorAll('.remove-process');
        removeProcessBtns.forEach(btn => {
            btn.addEventListener('click', (e) => {
                const processList = dialogEl.querySelector('#process-list');
                // 如果这是最后一个进程，禁止删除
                if (processList.querySelectorAll('.process-item').length <= 1) {
                    e.preventDefault();
                    return;
                }

                e.target.closest('.process-item').remove();

                // 如果现在只剩一个进程，禁用其删除按钮
                if (processList.querySelectorAll('.process-item').length <= 1) {
                    const lastRemoveBtn = processList.querySelector('.remove-process');
                    if (lastRemoveBtn) {
                        lastRemoveBtn.disabled = true;
                    }
                }
            });
        });

        // 保存按钮
        const saveBtn = dialogEl.querySelector('#save-edit');
        if (saveBtn) {
            saveBtn.addEventListener('click', () => this.saveEditDialog());
        }

        // 取消按钮
        const cancelBtn = dialogEl.querySelector('#cancel-edit');
        if (cancelBtn) {
            cancelBtn.addEventListener('click', () => {
                dialogEl.close();
                this.selectedApp = null;
            });
        }

        // 点击对话框外部关闭
        dialogEl.addEventListener('click', (e) => {
            if (e.target === dialogEl) {
                dialogEl.close();
                this.selectedApp = null;
            }
        });
    },

    // 添加进程输入
    addProcessInput(packageName) {
        const processList = document.getElementById('process-list');
        if (!processList) return;

        const newIndex = processList.querySelectorAll('.process-item').length;
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

        // 启用所有移除按钮（现在有多个进程了）
        const removeButtons = processList.querySelectorAll('.remove-process');
        removeButtons.forEach(btn => {
            btn.disabled = false;
        });

        // 绑定删除按钮事件
        const removeBtn = newProcessItem.querySelector('.remove-process');
        if (removeBtn) {
            removeBtn.addEventListener('click', (e) => {
                // 如果这是最后一个进程，禁止删除
                if (processList.querySelectorAll('.process-item').length <= 1) {
                    e.preventDefault();
                    return;
                }

                newProcessItem.remove();

                // 如果现在只剩一个进程，禁用其删除按钮
                if (processList.querySelectorAll('.process-item').length <= 1) {
                    const lastRemoveBtn = processList.querySelector('.remove-process');
                    if (lastRemoveBtn) {
                        lastRemoveBtn.disabled = true;
                    }
                }
            });
        }

        // 聚焦新添加的输入框
        const input = newProcessItem.querySelector('.process-input');
        if (input) {
            input.focus();
            input.select();
        }
    },

    // 保存编辑对话框
    saveEditDialog() {
        if (!this.selectedApp) return;

        const processInputs = document.querySelectorAll('.process-input');
        const processes = Array.from(processInputs).map(input => input.value.trim()).filter(Boolean);

        if (processes.length === 0) {
            Core.showToast(I18n.translate('PROCESS_REQUIRED', '至少需要一个进程'), 'warning');
            return;
        }

        // 更新配置
        this.configData.suppress_apps[this.selectedApp].processes = processes;

        // 标记为有未保存的更改
        this.hasUnsavedChanges = true;

        // 关闭对话框并更新UI
        const dialogEl = document.getElementById('app-dialog');
        if (dialogEl) dialogEl.close();

        this.selectedApp = null;
        this.updateUI();

        Core.showToast(I18n.translate('CONFIG_UPDATED', '配置已更新'), 'success');
    },

    // 语言切换处理
    onLanguageChanged() {
        this.updateUI();
    },
    
    // 优化UI更新
    updateUI() {
        if (!this.isVisible) return; // 页面不可见时不更新
        
        // 使用 requestAnimationFrame 优化渲染
        if (this.updateRAF) {
            cancelAnimationFrame(this.updateRAF);
        }
        
        this.updateRAF = requestAnimationFrame(() => {
            const container = document.querySelector('.suppress-manager-container');
            if (container) {
                container.innerHTML = this.render();
                this.bindEvents();
            }
        });
    },
    
    // 页面可见性变化处理
    handleVisibilityChange() {
        this.isVisible = document.visibilityState === 'visible';
        if (this.isVisible && this.hasUnsavedChanges) {
            this.updateUI();
        }
    },

    // 显示加载中
    setLoadingState(isLoading) {
        this.isLoading = isLoading;

        const loadingElement = document.getElementById('settings-loading');
        if (loadingElement) {
            if (isLoading) {
                loadingElement.style.display = 'flex';
                loadingElement.style.visibility = 'visible';
                loadingElement.style.opacity = '1';
            } else {
                loadingElement.style.opacity = '0';
                loadingElement.style.visibility = 'hidden';

                setTimeout(() => {
                    if (!this.isLoading) {
                        loadingElement.style.display = 'none';
                    }
                }, 300);
            }
        }
    },

    // 注册操作按钮
    registerActions() {
        UI.registerPageActions('suppress_manager', [
            {
                id: 'save-config',
                icon: 'save',
                title: I18n.translate('SAVE', '保存'),
                onClick: 'saveConfig'
            },
            {
                id: 'restore-config',
                icon: 'restore',
                title: I18n.translate('RESTORE_CONFIG', '恢复设置'),
                onClick: 'restoreConfig'
            },
            {
                id: 'help-button',
                icon: 'help',
                title: I18n.translate('HELP', '帮助'),
                onClick: 'showHelp'
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

            this.setLoadingState(true);
            const [configData, installedApps] = await Promise.all([
                this.loadConfig(),
                this.loadInstalledApps()
            ]);

            this.configData = configData;
            this.installedApps = installedApps;

            this.updateUI();
            Core.showToast(I18n.translate('CONFIG_REFRESHED', '配置已刷新'), 'success');
        } catch (error) {
            console.error('刷新配置失败:', error);
            Core.showToast(I18n.translate('CONFIG_REFRESH_ERROR', '刷新配置失败'), 'error');
        } finally {
            this.setLoadingState(false);
        }
    },

    // 检查是否有未保存的更改
    hasUnsavedChanges() {
        // 这里可以实现对比原始配置和当前配置的逻辑
        // 简化版本直接返回false，后续可以扩展完善
        return false;
    },

    // 激活页面
    onActivate() {
        // 重置取消标志
        this.isCancelled = false;
    },

    // 显示添加配置弹窗
    showAddConfigDialog() {
        const dialogEl = document.getElementById('app-dialog');
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
        const dialogEl = document.getElementById('app-dialog');
        if (dialogEl) {
            dialogEl.close();
            this.searchQuery = ''; // 重置搜索查询
        }
    },

    // 加载可用应用列表
    async loadAvailableApps() {
        this.renderAvailableApps();
    },

    // 保存临时配置状态
    saveTemporaryConfigState() {
        if (!this.hasConfigChanged()) return;
        // 使用本地存储保存临时状态
        try {
            localStorage.setItem('suppress_manager_temp_config', JSON.stringify(this.configData));
        } catch (e) {
            console.error('保存临时配置失败:', e);
        }
    },

    // 恢复临时配置状态
    restoreTemporaryConfigState() {
        try {
            const tempConfig = localStorage.getItem('suppress_manager_temp_config');
            if (tempConfig) {
                this.configData = JSON.parse(tempConfig);
                localStorage.removeItem('suppress_manager_temp_config');
                return true;
            }
        } catch (e) {
            console.error('恢复临时配置失败:', e);
        }
        return false;
    },

    // 加载CSS样式
    loadCss() {
        const cssPath = 'css/pages/suppress_manager.css';

        // 检查是否已经加载
        const existingLink = document.querySelector(`link[href="${cssPath}"]`);
        if (existingLink) return;

        // 创建并添加CSS链接
        const link = document.createElement('link');
        link.rel = 'stylesheet';
        link.type = 'text/css';
        link.href = cssPath;
        document.head.appendChild(link);
    },

    // 显示帮助信息
    showHelp() {
        Core.showToast(I18n.translate('SUPPRESS_HELP', '抑制管理器用于配置应用进程抑制。点击右下角按钮添加新配置。'), 'info', 5000);
    },

    // 恢复初始配置
    async restoreConfig() {
        if (!this.configBackup) {
            Core.showToast(I18n.translate('NO_CONFIG_BACKUP', '没有可用的配置备份'), 'warning');
            return;
        }

        const confirmRestore = confirm(I18n.translate('CONFIRM_RESTORE', '确定要恢复初始配置吗？'));
        if (!confirmRestore) return;

        this.configData = JSON.parse(JSON.stringify(this.configBackup));
        this.hasUnsavedChanges = false;
        this.updateUI();

        Core.showToast(I18n.translate('CONFIG_RESTORED', '配置已恢复'), 'success');
    },

    // 检查配置是否有变化
    hasConfigChanged() {
        if (!this.configBackup) return false;
        return JSON.stringify(this.configData) !== JSON.stringify(this.configBackup);
    },

    // 停用页面
    onDeactivate() {
        // 如果配置有变化但未保存，提示用户
        if (this.hasUnsavedChanges) {
            const confirmLeave = confirm(I18n.translate('UNSAVED_CHANGES', '有未保存的更改，确定要离开吗？'));
            if (!confirmLeave) {
                return false;
            }
        }

        // 注销语言切换处理器
        I18n.unregisterLanguageChangeHandler(this.onLanguageChanged.bind(this));

        // 清理页面操作按钮
        UI.clearPageActions('suppress_manager');

        // 设置取消标志，用于中断正在进行的异步操作
        this.isCancelled = true;

        return true;
    },

    // HTML转义
    escapeHtml(unsafe) {
        return unsafe
            ? unsafe.toString()
                .replace(/&/g, "&amp;")
                .replace(/</g, "&lt;")
                .replace(/>/g, "&gt;")
                .replace(/"/g, "&quot;")
                .replace(/'/g, "&#039;")
            : '';
    },

    // 渲染后回调
    afterRender() {
        // 初始化加载数据
        this.init().then(() => {
            // 更新UI显示
            this.updateUI();

            // 加载CSS样式
            this.loadCss();
        });
    },

    // 绑定事件
    bindEvents() {
        // 绑定FAB按钮事件
        const addConfigButton = document.getElementById('add-config-button');
        if (addConfigButton) {
            addConfigButton.addEventListener('click', () => {
                this.showAppSelectionDialog();
            });
        }

        // 绑定已配置应用列表的事件
        const configuredAppsList = document.getElementById('configured-apps');
        if (configuredAppsList) {
            // 切换应用启用状态
            configuredAppsList.querySelectorAll('.toggle-app').forEach(toggle => {
                toggle.addEventListener('change', (e) => {
                    const packageName = e.target.dataset.package;
                    if (packageName) {
                        this.toggleAppEnabled(packageName);
                    }
                });
            });

            // 编辑按钮
            configuredAppsList.querySelectorAll('.edit-app').forEach(btn => {
                btn.addEventListener('click', (e) => {
                    const packageName = e.target.closest('[data-package]').dataset.package;
                    if (packageName) {
                        this.showEditDialog(packageName);
                    }
                });
            });

            // 删除按钮
            configuredAppsList.querySelectorAll('.delete-app').forEach(btn => {
                btn.addEventListener('click', (e) => {
                    const packageName = e.target.closest('[data-package]').dataset.package;
                    if (packageName && confirm(I18n.translate('CONFIRM_DELETE', '确定要删除此应用配置吗？'))) {
                        this.removeAppConfig(packageName);
                    }
                });
            });
        }
    },

    // 绑定进程配置对话框事件
    bindProcessConfigEvents(dialogEl, packageName) {
        // 关闭按钮
        const closeBtn = dialogEl.querySelector('#close-dialog');
        if (closeBtn) {
            closeBtn.addEventListener('click', () => {
                dialogEl.close();
                this.selectedApp = null;
            });
        }

        // 添加进程按钮
        const addProcessBtn = dialogEl.querySelector('#add-process-btn');
        if (addProcessBtn) {
            addProcessBtn.addEventListener('click', () => this.addProcessInput(packageName));
        }

        // 保存按钮
        const saveBtn = dialogEl.querySelector('#save-config-btn');
        if (saveBtn) {
            saveBtn.addEventListener('click', () => {
                const processInputs = dialogEl.querySelectorAll('.process-input');
                const processes = Array.from(processInputs).map(input => input.value.trim()).filter(Boolean);

                if (processes.length === 0) {
                    Core.showToast(I18n.translate('PROCESS_REQUIRED', '至少需要一个进程'), 'warning');
                    return;
                }

                // 添加新配置
                this.addAppConfig(packageName, processes);

                // 关闭对话框
                dialogEl.close();
                this.selectedApp = null;

                Core.showToast(I18n.translate('CONFIG_ADDED', '配置已添加'), 'success');
            });
        }

        // 取消按钮
        const cancelBtn = dialogEl.querySelector('#cancel-config');
        if (cancelBtn) {
            cancelBtn.addEventListener('click', () => {
                dialogEl.close();
                this.selectedApp = null;
                this.showAppSelectionDialog();
            });
        }

        // 点击对话框外部关闭
        dialogEl.addEventListener('click', (e) => {
            if (e.target === dialogEl) {
                dialogEl.close();
                this.selectedApp = null;
            }
        });
    },
};
// 导出模块
window.SuppressManagerPage = SuppressManagerPage;