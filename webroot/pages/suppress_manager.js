/**
 * AMMF WebUI 抑制管理器页面模块
 * 管理DeepSuppressor应用抑制配置
 */

const SuppressManagerPage = {
    // 状态和数据
    configData: { suppress_apps: {} },
    installedApps: [],
    isLoading: false,
    isLoadingApps: false,
    searchQuery: '',
    selectedApp: null,
    
    // 配置项
    config: {
        configPath: `${Core.MODULE_PATH}module_settings/suppress_config.json`
    },
    
    // 预加载数据
    async preloadData() {
        try {
            // 预加载配置数据和应用列表
            const configData = await this.loadConfig();
            // 开始加载应用列表，但不等待完成
            this.loadInstalledApps();
            return { configData };
        } catch (error) {
            console.error('预加载抑制管理器数据失败:', error);
            return { configData: { suppress_apps: {} } };
        }
    },
    
    // 初始化
    async init() {
        try {
            // 显示加载状态
            this.setLoadingState(true);
            
            // 获取预加载的数据或重新加载
            const preloadedData = PreloadManager.getData('suppress_manager');
            
            if (preloadedData && preloadedData.configData) {
                this.configData = preloadedData.configData;
            } else {
                // 加载配置
                await this.loadConfig();
            }
            
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
            // 使用命令行读取配置文件
            const configContent = await Core.execCommand(`cat ${this.config.configPath}`);
            try {
                this.configData = JSON.parse(configContent.trim()) || { suppress_apps: {} };
                // 确保 suppress_apps 存在
                if (!this.configData.suppress_apps) {
                    this.configData.suppress_apps = {};
                }
            } catch (parseError) {
                console.error('解析配置失败:', parseError);
                this.configData = { suppress_apps: {} };
            }
            return this.configData;
        } catch (error) {
            console.error('加载配置失败:', error);
            this.configData = { suppress_apps: {} };
            return this.configData;
        }
    },
    
    // 加载已安装应用列表
    async loadInstalledApps() {
        if (this.isLoadingApps) return this.installedApps;
        
        try {
            this.isLoadingApps = true;
            this.updateUI(); // 更新UI显示加载状态
            Core.showToast(I18n.translate('LOADING_APPS', '正在加载应用列表...'), 'info');
            
            // 获取第三方应用列表
            const appListCmd = await Core.execCommand('pm list packages -3');
            const appList = this.parsePackageList(appListCmd);
            
            // 获取应用名称
            this.installedApps = [];
            for (const packageName of appList) {
                try {
                    const appInfo = await this.getAppInfo(packageName);
                    this.installedApps.push(appInfo);
                } catch (e) {
                    // 如果获取应用信息失败，使用默认信息
                    this.installedApps.push(this.createDefaultAppInfo(packageName));
                }
            }
            
            // 按应用名称排序
            this.installedApps.sort((a, b) => a.appName.localeCompare(b.appName));
            
            this.isLoadingApps = false;
            this.updateUI();
            Core.showToast(I18n.translate('APPS_LOADED', '应用列表加载完成'), 'success');
            return this.installedApps;
        } catch (error) {
            console.error('加载已安装应用失败:', error);
            this.isLoadingApps = false;
            Core.showToast(I18n.translate('LOAD_APPS_FAILED', '加载应用列表失败'), 'error');
            return [];
        }
    },
    
    // 解析包列表
    parsePackageList(output) {
        return output.split('\n')
            .filter(line => line.trim().startsWith('package:'))
            .map(line => line.trim().substring(8));
    },
    
    // 获取应用信息
    async getAppInfo(packageName) {
        try {
            // 改进应用名称获取方法
            const appLabelCmd = await Core.execCommand(`dumpsys package ${packageName} | grep -E "labelRes=|nonLocalizedLabel="`);
            let appName = packageName.split('.').pop();
            
            // 尝试从nonLocalizedLabel获取名称
            const nonLocalizedMatch = appLabelCmd.match(/nonLocalizedLabel=([^\s]+)/i);
            if (nonLocalizedMatch && nonLocalizedMatch[1]) {
                appName = nonLocalizedMatch[1];
            }
            
            return {
                packageName,
                appName,
                isSystem: false
            };
        } catch (error) {
            return this.createDefaultAppInfo(packageName);
        }
    },
    
    // 创建默认应用信息
    createDefaultAppInfo(packageName) {
        return {
            packageName,
            appName: packageName.split('.').pop() || packageName,
            isSystem: false
        };
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
            } catch (error) {
                console.error('保存配置文件失败:', error);
                Core.showToast(I18n.translate('CONFIG_SAVE_ERROR', '保存配置失败'), 'error');
                return false;
            }
            
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
        }
        
        return true;
    },
    
    // 添加应用配置
    addAppConfig(packageName, processes = []) {
        if (!packageName || !packageName.trim()) {
            Core.showToast(I18n.translate('INVALID_PACKAGE', '无效的包名'), 'error');
            return;
        }
        
        if (!this.configData.suppress_apps) {
            this.configData.suppress_apps = {};
        }
        
        // 如果没有提供进程，使用默认进程名
        if (!processes || processes.length === 0) {
            processes = [`${packageName}:appbrand0`];
        }
        
        this.configData.suppress_apps[packageName] = {
            enabled: true,
            processes: processes
        };
        
        this.updateUI();
        Core.showToast(I18n.translate('APP_CONFIG_ADDED', '应用配置已添加'), 'success');
    },
    
    // 移除应用配置
    removeAppConfig(packageName) {
        if (this.configData.suppress_apps && this.configData.suppress_apps[packageName]) {
            delete this.configData.suppress_apps[packageName];
            this.updateUI();
            Core.showToast(I18n.translate('APP_CONFIG_REMOVED', '应用配置已移除'), 'success');
        }
    },
    
    // 切换应用启用状态
    toggleAppEnabled(packageName) {
        if (this.configData.suppress_apps && this.configData.suppress_apps[packageName]) {
            this.configData.suppress_apps[packageName].enabled =
                !this.configData.suppress_apps[packageName].enabled;
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
        if (!processName || !processName.trim()) {
            Core.showToast(I18n.translate('INVALID_PROCESS', '无效的进程名'), 'error');
            return false;
        }
        
        if (this.configData.suppress_apps && this.configData.suppress_apps[packageName]) {
            // 检查进程是否已存在
            if (this.configData.suppress_apps[packageName].processes.includes(processName)) {
                Core.showToast(I18n.translate('PROCESS_EXISTS', '进程已存在'), 'warning');
                return false;
            }
            
            this.configData.suppress_apps[packageName].processes.push(processName);
            this.updateUI();
            Core.showToast(I18n.translate('PROCESS_ADDED', '进程已添加'), 'success');
            return true;
        }
        return false;
    },
    
    // 移除进程
    removeProcess(packageName, processName) {
        if (this.configData.suppress_apps && this.configData.suppress_apps[packageName]) {
            const processes = this.configData.suppress_apps[packageName].processes;
            const index = processes.indexOf(processName);
            
            if (index !== -1) {
                processes.splice(index, 1);
                
                // 如果没有进程，添加一个默认进程
                if (processes.length === 0) {
                    processes.push(`${packageName}:appbrand0`);
                }
                
                this.updateUI();
                Core.showToast(I18n.translate('PROCESS_REMOVED', '进程已移除'), 'success');
                return true;
            }
        }
        return false;
    },
    
    // 更新进程
    updateProcesses(packageName, processes) {
        if (!Array.isArray(processes) || processes.length === 0) {
            Core.showToast(I18n.translate('INVALID_PROCESSES', '无效的进程列表'), 'error');
            return false;
        }
        
        if (this.configData.suppress_apps && this.configData.suppress_apps[packageName]) {
            this.configData.suppress_apps[packageName].processes = processes;
            this.updateUI();
            Core.showToast(I18n.translate('PROCESSES_UPDATED', '进程已更新'), 'success');
            return true;
        }
        return false;
    },
    
    // 渲染页面
    render() {
        return `
            <div class="suppress-manager-container">
                
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
                
                <!-- 浮动操作按钮 -->
                <div class="fab-container">
                    <button class="fab-button" id="add-app-fab">
                        <span class="material-symbols-rounded">add</span>
                    </button>
                </div>
                
                <!-- 加载指示器 -->
                <div id="settings-loading" class="loading-overlay">
                    <div class="loading-spinner"></div>
                </div>
            </div>
            
            <!-- 添加应用对话框 -->
            <div class="dialog-overlay" id="add-app-dialog" style="display: none;">
                <div class="dialog">
                    <div class="dialog-header">
                        <h3>${I18n.translate('ADD_APP', '添加应用')}</h3>
                        <button class="dialog-close" id="close-add-dialog">
                            <span class="material-symbols-rounded">close</span>
                        </button>
                    </div>
                    <div class="dialog-content">
                        <div class="search-container">
                            <label>
                                <span>"${I18n.translate('SEARCH_APPS', '搜索应用')}"</span>
                                <input type="text" id="app-search">
                            </label>
                            <button class="button" id="load-apps-btn">
                                ${I18n.translate('LOAD_APP_LIST', '加载应用列表')}
                            </button>
                        </div>
                        <div class="app-list available-app-list" id="available-apps">
                            ${this.isLoadingApps ? 
                                `<div class="loading-placeholder">${I18n.translate('LOADING_APPS', '正在加载应用...')}</div>` : 
                                this.renderAvailableApps()}
                        </div>
                        <div class="manual-add">
                            <div class="nput-field">
                                <input type="text" id="package-input" placeholder="${I18n.translate('PACKAGE_NAME', '包名')}">
                            </div>
                            <button class="button filled" id="add-app-btn">
                                ${I18n.translate('ADD', '添加')}
                            </button>
                        </div>
                    </div>
                </div>
            </div>
            
            <!-- 编辑进程对话框 -->
            <div class="dialog-overlay" id="edit-processes-dialog" style="display: none;">
                <div class="dialog">
                    <div class="dialog-header">
                        <h3>${I18n.translate('EDIT_PROCESSES', '编辑进程')}</h3>
                        <button class="dialog-close" id="close-edit-dialog">
                            <span class="material-symbols-rounded">close</span>
                        </button>
                    </div>
                    <div class="dialog-content">
                        <div class="app-info" id="edit-app-info"></div>
                        <div class="process-list" id="process-list"></div>
                        <div class="process-actions">
                            <button class="button" id="add-process-btn">
                                <span class="material-symbols-rounded">add</span>
                                ${I18n.translate('ADD_PROCESS', '添加进程')}
                            </button>
                        </div>
                        <div class="dialog-actions">
                            <button class="button" id="cancel-edit-btn">
                                ${I18n.translate('CANCEL', '取消')}
                            </button>
                            <button class="button filled" id="save-processes-btn">
                                ${I18n.translate('SAVE', '保存')}
                            </button>
                        </div>
                    </div>
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
            const isEnabled = config.enabled;
            
            html += `
                <div class="app-card configured-app ${isEnabled ? 'enabled' : 'disabled'}">
                    <div class="app-icon-placeholder"><span class="material-symbols-rounded">android</span></div>
                    <div class="app-info">
                        <div class="app-name">${this.escapeHtml(packageName.split('.').pop() || packageName)}</div>
                        <div class="package-name">${this.escapeHtml(packageName)}</div>
                        <div class="process-count">${I18n.translate('PROCESSES', '进程')}: ${config.processes.length}</div>
                    </div>
                    <div class="app-actions">
                        <button class="icon-button edit-processes" data-package="${packageName}">
                            <span class="material-symbols-rounded">edit</span>
                        </button>
                        <label class="toggle-switch">
                            <input type="checkbox" class="toggle-app" data-package="${packageName}" ${isEnabled ? 'checked' : ''}>
                            <span></span>
                        </label>
                        <button class="icon-button delete-app" data-package="${packageName}">
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
        if (!this.installedApps || this.installedApps.length === 0) {
            return `<div class="empty-state">${I18n.translate('NO_APPS_FOUND', '未找到应用')}</div>`;
        }
        
        // 过滤已配置的应用和搜索查询
        const configuredPackages = Object.keys(this.configData.suppress_apps || {});
        let filteredApps = this.installedApps.filter(app => 
            !configuredPackages.includes(app.packageName)
        );
        
        // 应用搜索过滤
        if (this.searchQuery) {
            const query = this.searchQuery.toLowerCase();
            filteredApps = filteredApps.filter(app => 
                app.appName.toLowerCase().includes(query) || 
                app.packageName.toLowerCase().includes(query)
            );
        }
        
        if (filteredApps.length === 0) {
            return `<div class="empty-state">${I18n.translate('NO_MATCHING_APPS', '没有匹配的应用')}</div>`;
        }
        
        let html = '';
        for (const app of filteredApps) {
            html += `
                <div class="app-card available-app" data-package="${app.packageName}" data-name="${this.escapeHtml(app.appName)}">
                    <div class="app-icon-placeholder"><span class="material-symbols-rounded">android</span></div>
                    <div class="app-info">
                        <div class="app-name">${this.escapeHtml(app.appName)}</div>
                        <div class="package-name">${this.escapeHtml(app.packageName)}</div>
                    </div>
                </div>
            `;
        }
        
        return html;
    },
    
    // 渲染进程编辑对话框内容
    renderProcessList(packageName) {
        if (!this.configData.suppress_apps || !this.configData.suppress_apps[packageName]) {
            return '';
        }
        
        const processes = this.configData.suppress_apps[packageName].processes;
        let html = '';
        
        for (let i = 0; i < processes.length; i++) {
            html += `
                <div class="process-item" data-index="${i}">
                    <div class="md3-input-field">
                        <input type="text" class="process-input" value="${this.escapeHtml(processes[i])}">
                    </div>
                    <button class="icon-button remove-process">
                        <span class="material-symbols-rounded">delete</span>
                    </button>
                </div>
            `;
        }
        
        return html;
    },
    
    // 显示添加应用对话框
    showAddAppDialog() {
        const dialog = document.getElementById('add-app-dialog');
        if (dialog) {
            dialog.style.display = 'flex';
            
            // 如果应用列表为空，自动加载应用列表
            if (!this.installedApps || this.installedApps.length === 0) {
                this.loadInstalledApps();
            }
            
            // 聚焦搜索框
            const searchInput = document.getElementById('app-search');
            if (searchInput) {
                searchInput.focus();
            }
        }
    },
    
    // 隐藏添加应用对话框
    hideAddAppDialog() {
        const dialog = document.getElementById('add-app-dialog');
        if (dialog) {
            dialog.style.display = 'none';
            
            // 清空搜索和输入
            const searchInput = document.getElementById('app-search');
            if (searchInput) {
                searchInput.value = '';
            }
            
            const packageInput = document.getElementById('package-input');
            if (packageInput) {
                packageInput.value = '';
            }
            
            this.searchQuery = '';
        }
    },
    
    // 显示编辑进程对话框
    showEditProcessesDialog(packageName) {
        if (!this.configData.suppress_apps || !this.configData.suppress_apps[packageName]) {
            return;
        }
        
        this.selectedApp = packageName;
        
        const dialog = document.getElementById('edit-processes-dialog');
        const appInfoElement = document.getElementById('edit-app-info');
        const processListElement = document.getElementById('process-list');
        
        if (dialog && appInfoElement && processListElement) {
            // 设置应用信息
            appInfoElement.innerHTML = `
                <div class="app-name">${this.escapeHtml(packageName.split('.').pop() || packageName)}</div>
                <div class="package-name">${this.escapeHtml(packageName)}</div>
            `;
            
            // 渲染进程列表
            processListElement.innerHTML = this.renderProcessList(packageName);
            
            // 显示对话框
            dialog.style.display = 'flex';
            
            // 绑定进程项事件
            this.bindProcessItemEvents();
        }
    },
    
    // 隐藏编辑进程对话框
    hideEditProcessesDialog() {
        const dialog = document.getElementById('edit-processes-dialog');
        if (dialog) {
            dialog.style.display = 'none';
            this.selectedApp = null;
        }
    },
    
    // 添加新进程输入框
    addProcessInput() {
        if (!this.selectedApp) return;
        
        const processListElement = document.getElementById('process-list');
        if (processListElement) {
            const newIndex = processListElement.querySelectorAll('.process-item').length;
            const newProcessItem = document.createElement('div');
            newProcessItem.className = 'process-item';
            newProcessItem.dataset.index = newIndex;
            
            newProcessItem.innerHTML = `
                <label>
                    <span>"Process"</span>
                    <input type="text" id="process-input" value="${this.selectedApp}:">
                </label>
                <button class="icon-button remove-process">
                    <span class="material-symbols-rounded">delete</span>
                </button>
            `;
            
            processListElement.appendChild(newProcessItem);
            
            // 绑定新添加的进程项事件
            this.bindProcessItemEvents(newProcessItem);
            
            // 聚焦新输入框
            const input = newProcessItem.querySelector('.process-input');
            if (input) {
                input.focus();
                // 将光标移动到末尾
                const length = input.value.length;
                input.setSelectionRange(length, length);
            }
        }
    },
    
    // 保存进程编辑
    saveProcesses() {
        if (!this.selectedApp) return;
        
        const processInputs = document.querySelectorAll('#process-list .process-input');
        const processes = [];
        
        // 收集所有非空进程
        processInputs.forEach(input => {
            const value = input.value.trim();
            if (value) {
                processes.push(value);
            }
        });
        
        // 至少需要一个进程
        if (processes.length === 0) {
            Core.showToast(I18n.translate('NEED_ONE_PROCESS', '至少需要一个进程'), 'warning');
            return;
        }
        
        // 更新进程列表
        if (this.updateProcesses(this.selectedApp, processes)) {
            this.hideEditProcessesDialog();
        }
    },
    
    // 语言切换处理
    onLanguageChanged() {
        this.updateUI();
    },
    
    // 更新UI
    updateUI() {
        // 使用requestAnimationFrame优化UI更新
        window.requestAnimationFrame(() => {
            const container = document.querySelector('.suppress-manager-container');
            if (container) {
                // 保存滚动位置
                const scrollTop = container.scrollTop;
                
                // 更新内容
                container.innerHTML = this.render();
                
                // 恢复滚动位置
                container.scrollTop = scrollTop;
                
                // 重新绑定事件
                this.bindEvents();
            }
        });
    },
    
    // 显示/隐藏加载状态
    setLoadingState(isLoading) {
        this.isLoading = isLoading;
        
        const loadingElement = document.getElementById('settings-loading');
        if (loadingElement) {
            loadingElement.style.display = isLoading ? 'flex' : 'none';
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
                id: 'help-button',
                icon: 'help',
                title: I18n.translate('HELP', '帮助'),
                onClick: 'showHelp'
            }
        ]);
    },
    
    // 显示帮助信息
    showHelp() {
        Core.showToast(I18n.translate('SUPPRESS_HELP', '抑制管理器用于配置应用进程抑制。点击+添加应用，点击编辑按钮修改进程。'), 'info', 5000);
    },
    
    // 激活页面
    onActivate() {
        // 页面激活时的处理
    },
    
    // 停用页面
    onDeactivate() {
        // 注销语言切换处理器
        I18n.unregisterLanguageChangeHandler(this.onLanguageChanged.bind(this));
        
        // 清理页面操作按钮
        UI.clearPageActions('suppress_manager');
        
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
        });
    },
    
    // 绑定事件
    bindEvents() {
        // 绑定FAB按钮
        const addAppFab = document.getElementById('add-app-fab');
        if (addAppFab) {
            addAppFab.addEventListener('click', () => {
                this.showAddAppDialog();
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
            
            // 删除按钮
            configuredAppsList.querySelectorAll('.delete-app').forEach(btn => {
                btn.addEventListener('click', (e) => {
                    const packageName = e.target.closest('[data-package]').dataset.package;
                    if (packageName && confirm(I18n.translate('CONFIRM_DELETE', '确定要删除此应用配置吗？'))) {
                        this.removeAppConfig(packageName);
                    }
                });
            });
            
            // 编辑进程按钮
            configuredAppsList.querySelectorAll('.edit-processes').forEach(btn => {
                btn.addEventListener('click', (e) => {
                    const packageName = e.target.closest('[data-package]').dataset.package;
                    if (packageName) {
                        this.showEditProcessesDialog(packageName);
                    }
                });
            });
        }
        
        // 绑定添加应用对话框事件
        this.bindAddAppDialogEvents();
        
        // 绑定编辑进程对话框事件
        this.bindEditProcessesDialogEvents();
    },
    
    // 绑定添加应用对话框事件
    bindAddAppDialogEvents() {
        // 关闭对话框按钮
        const closeAddDialog = document.getElementById('close-add-dialog');
        if (closeAddDialog) {
            closeAddDialog.addEventListener('click', () => {
                this.hideAddAppDialog();
            });
        }
        
        // 加载应用列表按钮
        const loadAppsBtn = document.getElementById('load-apps-btn');
        if (loadAppsBtn) {
            loadAppsBtn.addEventListener('click', async () => {
                await this.loadInstalledApps();
            });
        }
        
        // 搜索输入框
        const searchInput = document.getElementById('app-search');
        if (searchInput) {
            searchInput.addEventListener('input', (e) => {
                this.searchQuery = e.target.value.trim();
                
                // 更新可用应用列表
                const availableAppsList = document.getElementById('available-apps');
                if (availableAppsList) {
                    availableAppsList.innerHTML = this.renderAvailableApps();
                    this.bindAvailableAppsEvents();
                }
            });
        }
        
        // 手动添加应用按钮
        const addAppBtn = document.getElementById('add-app-btn');
        if (addAppBtn) {
            addAppBtn.addEventListener('click', () => {
                const packageInput = document.getElementById('package-input');
                if (packageInput && packageInput.value.trim()) {
                    this.addAppConfig(packageInput.value.trim());
                    this.hideAddAppDialog();
                } else {
                    Core.showToast(I18n.translate('ENTER_PACKAGE_NAME', '请输入包名'), 'warning');
                }
            });
        }
        
        // 绑定可用应用列表事件
        this.bindAvailableAppsEvents();
    },
    
    // 绑定可用应用列表事件
    bindAvailableAppsEvents() {
        const availableAppsList = document.getElementById('available-apps');
        if (availableAppsList) {
            availableAppsList.querySelectorAll('.available-app').forEach(appCard => {
                appCard.addEventListener('click', () => {
                    const packageName = appCard.dataset.package;
                    if (packageName) {
                        this.addAppConfig(packageName);
                        this.hideAddAppDialog();
                        
                        // 添加后立即显示编辑进程对话框
                        setTimeout(() => {
                            this.showEditProcessesDialog(packageName);
                        }, 300);
                    }
                });
            });
        }
    },
    
    // 绑定编辑进程对话框事件
    bindEditProcessesDialogEvents() {
        // 关闭对话框按钮
        const closeEditDialog = document.getElementById('close-edit-dialog');
        if (closeEditDialog) {
            closeEditDialog.addEventListener('click', () => {
                this.hideEditProcessesDialog();
            });
        }
        
        // 添加进程按钮
        const addProcessBtn = document.getElementById('add-process-btn');
        if (addProcessBtn) {
            addProcessBtn.addEventListener('click', () => {
                this.addProcessInput();
            });
        }
        
        // 取消按钮
        const cancelEditBtn = document.getElementById('cancel-edit-btn');
        if (cancelEditBtn) {
            cancelEditBtn.addEventListener('click', () => {
                this.hideEditProcessesDialog();
            });
        }
        
        // 保存按钮
        const saveProcessesBtn = document.getElementById('save-processes-btn');
        if (saveProcessesBtn) {
            saveProcessesBtn.addEventListener('click', () => {
                this.saveProcesses();
            });
        }
    },
    
    // 绑定进程项事件
    bindProcessItemEvents(container = null) {
        const selector = container ? '.remove-process' : '#process-list .remove-process';
        const buttons = container ? container.querySelectorAll(selector) : document.querySelectorAll(selector);
        
        buttons.forEach(btn => {
            btn.addEventListener('click', (e) => {
                const processItem = e.target.closest('.process-item');
                if (processItem) {
                    // 如果只有一个进程项，不允许删除
                    const processItems = document.querySelectorAll('#process-list .process-item');
                    if (processItems.length <= 1) {
                        Core.showToast(I18n.translate('NEED_ONE_PROCESS', '至少需要一个进程'), 'warning');
                        return;
                    }
                    
                    processItem.remove();
                }
            });
        });
    }
};

// 导出模块
window.SuppressManagerPage = SuppressManagerPage;