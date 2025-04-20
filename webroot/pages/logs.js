/**
 * AMMF WebUI 日志页面模块
 * 提供日志查看和管理功能
 */

const LogsPage = {
    // 日志文件列表
    logFiles: {},
    
    // 当前选中的日志文件
    currentLogFile: '',
    
    // 日志内容
    logContent: '',
    
    // 自动刷新设置
    autoRefresh: false,
    refreshTimer: null,
    refreshInterval: 5000, // 5秒刷新一次
    
    // 初始化
    async init() {
        try {
            // 检查日志目录是否存在
            const logsDir = `${Core.MODULE_PATH}logs/`;
            const dirExists = await this.checkLogsDirectoryExists(logsDir);
            
            if (!dirExists) {
                console.warn(I18n.translate('LOGS_DIR_NOT_FOUND', '日志目录不存在'));
                this.logContent = I18n.translate('LOGS_DIR_NOT_FOUND', '日志目录不存在');
                return false;
            }
            
            // 扫描可用的日志文件
            await this.scanLogFiles();
            
            // 设置默认日志文件
            if (Object.keys(this.logFiles).length > 0) {
                this.currentLogFile = Object.keys(this.logFiles)[0];
                await this.loadLogContent();
            } else {
                this.logContent = I18n.translate('NO_LOGS_FILES', '没有找到日志文件');
            }
            
            return true;
        } catch (error) {
            console.error(I18n.translate('LOGS_INIT_ERROR', '初始化日志页面失败:'), error);
            this.logContent = I18n.translate('LOGS_INIT_ERROR', '初始化日志页面失败');
            return false;
        }
    },
    
    // 检查日志目录是否存在
    async checkLogsDirectoryExists(logsDir) {
        try {
            const result = await Core.execCommand(`[ -d "${logsDir}" ] && echo "true" || echo "false"`);
            return result.trim() === "true";
        } catch (error) {
            console.error(I18n.translate('LOGS_DIR_CHECK_ERROR', '检查日志目录失败:'), error);
            return false;
        }
    },
    
    // 扫描日志文件
    async scanLogFiles() {
        try {
            const logsDir = `${Core.MODULE_PATH}logs/`;
            
            // 检查目录是否存在
            const dirExists = await this.checkLogsDirectoryExists(logsDir);
            if (!dirExists) {
                console.warn(I18n.translate('LOGS_DIR_NOT_FOUND', '日志目录不存在'));
                this.logFiles = {};
                return;
            }
            
            // 获取logs目录下的所有日志文件
            const result = await Core.execCommand(`find "${logsDir}" -type f -name "*.log" -o -name "*.log.old" 2>/dev/null | sort`);
            
            // 清空现有日志文件列表
            this.logFiles = {};
            
            if (!result || result.trim() === '') {
                console.warn(I18n.translate('NO_LOGS_FILES', '没有找到日志文件'));
                return;
            }
            
            // 解析日志文件列表
            const files = result.split('\n').filter(file => file.trim() !== '');
            
            files.forEach(file => {
                const fileName = file.split('/').pop();
                this.logFiles[fileName] = file;
            });
            
            console.log(I18n.translate('LOGS_FILES_FOUND', '找到 {count} 个日志文件', {count: Object.keys(this.logFiles).length}));
        } catch (error) {
            console.error(I18n.translate('LOGS_SCAN_ERROR', '扫描日志文件失败:'), error);
            this.logFiles = {};
        }
    },
    
    // 加载日志内容
    async loadLogContent(showToast = false) {
        try {
            if (!this.currentLogFile || !this.logFiles[this.currentLogFile]) {
                this.logContent = I18n.translate('NO_LOG_SELECTED', '未选择日志文件');
                return;
            }
            
            // 使用setTimeout让UI有机会更新
            await new Promise(resolve => setTimeout(resolve, 0));
            
            const logPath = this.logFiles[this.currentLogFile];
            
            // 检查文件是否存在
            const fileExistsResult = await Core.execCommand(`[ -f "${logPath}" ] && echo "true" || echo "false"`);
            if (fileExistsResult.trim() !== "true") {
                this.logContent = I18n.translate('LOG_FILE_NOT_FOUND', '日志文件不存在');
                if (showToast) Core.showToast(this.logContent, 'warning');
                return;
            }
            
            // 显示加载指示器
            const logsDisplay = document.getElementById('logs-display');
            if (logsDisplay) {
                logsDisplay.classList.add('loading');
            }
            
            // 使用requestIdleCallback处理大数据
            await new Promise(resolve => {
                requestIdleCallback(async () => {
                    const content = await Core.execCommand(`cat "${logPath}"`);
                    this.logContent = content || I18n.translate('NO_LOGS', '没有可用的日志');
                    
                    // 更新显示
                    if (logsDisplay) {
                        logsDisplay.innerHTML = this.formatLogContent();
                        logsDisplay.classList.remove('loading');
                        // 滚动到底部
                        logsDisplay.scrollTop = logsDisplay.scrollHeight;
                    }
                    
                    if (showToast) Core.showToast(I18n.translate('LOGS_REFRESHED', '日志已刷新'));
                    resolve();
                });
            });
        } catch (error) {
            console.error(I18n.translate('LOGS_LOAD_ERROR', '加载日志内容失败:'), error);
            this.logContent = I18n.translate('LOGS_LOAD_ERROR', '加载失败');
            
            const logsDisplay = document.getElementById('logs-display');
            if (logsDisplay) {
                logsDisplay.classList.remove('loading');
            }
            
            if (showToast) Core.showToast(this.logContent, 'error');
        }
    },
    
    // 清除日志
    async clearLog() {
        try {
            if (!this.currentLogFile || !this.logFiles[this.currentLogFile]) {
                Core.showToast(I18n.translate('NO_LOG_SELECTED', '未选择日志文件'), 'warning');
                return;
            }
            
            const logPath = this.logFiles[this.currentLogFile];
            
            // 检查文件是否存在
            const fileExistsResult = await Core.execCommand(`[ -f "${logPath}" ] && echo "true" || echo "false"`);
            if (fileExistsResult.trim() !== "true") {
                Core.showToast(I18n.translate('LOG_FILE_NOT_FOUND', '日志文件不存在'), 'warning');
                return;
            }
            
            // 使用MD3对话框确认
            const dialog = document.createElement('dialog');
            dialog.className = 'md-dialog log-delete-dialog';
            dialog.innerHTML = `
                <h2>${I18n.translate('CLEAR_LOGS', '清除日志')}</h2>
                <p>${I18n.translate('CONFIRM_CLEAR_LOG', '确定要清除此日志文件吗？此操作不可撤销。')}</p>
                <div class="dialog-buttons">
                    <button class="dialog-button" data-action="cancel">${I18n.translate('CANCEL', '取消')}</button>
                    <button class="dialog-button filled" data-action="confirm">${I18n.translate('CONFIRM', '确认')}</button>
                </div>
            `;
            document.body.appendChild(dialog);
            
            // 显示对话框
            dialog.showModal();
            dialog.style.opacity = '0';
            requestAnimationFrame(() => {
                dialog.style.opacity = '1';
            });
            
            // 处理对话框按钮点击
            return new Promise((resolve, reject) => {
                dialog.addEventListener('click', async (e) => {
                    const action = e.target.getAttribute('data-action');
                    if (action === 'cancel' || action === 'confirm') {
                        // 添加关闭动画
                        dialog.classList.add('closing');
                        // 等待动画完成后关闭
                        setTimeout(() => {
                            dialog.close();
                            document.body.removeChild(dialog);
                        }, 200); // 与动画时长匹配

                        if (action === 'confirm') {
                            try {
                                // 清空日志文件
                                await Core.execCommand(`cat /dev/null > "${logPath}" && chmod 666 "${logPath}"`);
                                
                                // 重新加载日志内容
                                await this.loadLogContent();
                                
                                Core.showToast(I18n.translate('LOG_CLEARED', '日志已清除'));
                                resolve(true);
                            } catch (error) {
                                console.error(I18n.translate('LOG_CLEAR_ERROR', '清除日志失败:'), error);
                                Core.showToast(I18n.translate('LOG_CLEAR_ERROR', '清除日志失败'), 'error');
                                reject(error);
                            }
                        }
                    }
                });
            });
        } catch (error) {
            console.error(I18n.translate('LOG_CLEAR_ERROR', '清除日志失败:'), error);
            Core.showToast(I18n.translate('LOG_CLEAR_ERROR', '清除日志失败'), 'error');
            return false;
        }
    },
    
    // 导出日志
    async exportLog() {
        try {
            if (!this.currentLogFile || !this.logFiles[this.currentLogFile]) {
                Core.showToast(I18n.translate('NO_LOG_SELECTED', '未选择日志文件'), 'warning');
                return;
            }
            
            const logPath = this.logFiles[this.currentLogFile];
            
            // 使用cp命令复制到下载文件夹
            const downloadDir = '/sdcard/Download/';
            const timestamp = new Date().toISOString().replace(/[:.]/g, '-');
            const exportFileName = `${this.currentLogFile}_${timestamp}.log`;
            
            // 显示加载指示器
            Core.showToast(I18n.translate('LOADING', '导出中...'), 'info');
            
            // 确保下载目录存在并复制文件
            await Core.execCommand(`mkdir -p "${downloadDir}" && cp "${logPath}" "${downloadDir}${exportFileName}"`);
            
            Core.showToast(I18n.translate('LOG_EXPORTED', '日志已导出到: {path}', {path: `${downloadDir}${exportFileName}`}));
        } catch (error) {
            console.error(I18n.translate('LOG_EXPORT_ERROR', '导出日志失败:'), error);
            Core.showToast(I18n.translate('LOG_EXPORT_ERROR', '导出日志失败'), 'error');
        }
    },
    
    // 启动/停止自动刷新
    toggleAutoRefresh(enable) {
        if (enable) {
            if (this.refreshTimer) {
                clearInterval(this.refreshTimer);
            }
            this.autoRefresh = true;
            this.refreshTimer = setInterval(() => this.loadLogContent(), this.refreshInterval);
            console.log(I18n.translate('AUTO_REFRESH_STARTED', '自动刷新已启动'));
        } else {
            if (this.refreshTimer) {
                clearInterval(this.refreshTimer);
                this.refreshTimer = null;
            }
            this.autoRefresh = false;
            console.log(I18n.translate('AUTO_REFRESH_STOPPED', '自动刷新已停止'));
        }
    },
    
    // HTML转义
    escapeHtml(text) {
        if (!text) return '';
        return text
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
            .replace(/>/g, "&gt;")
            .replace(/"/g, "&quot;")
            .replace(/'/g, "&#039;");
    },
    
    // 格式化日志内容
    formatLogContent() {
        if (!this.logContent || this.logContent.trim() === '') {
            return `<div class="empty-state">${I18n.translate('NO_LOGS', '没有可用的日志')}</div>`;
        }
    
        // 将日志分割成行
        const lines = this.logContent.split('\n');
        const chunkSize = 1000; // 每次处理1000行
        const totalChunks = Math.ceil(lines.length / chunkSize);
        let processedContent = '';
    
        // 创建处理函数
        const processChunk = (chunkIndex) => {
            const start = chunkIndex * chunkSize;
            const end = Math.min(start + chunkSize, lines.length);
            
            for (let i = start; i < end; i++) {
                const line = lines[i];
                if (line.includes('[ERROR]') || line.includes('[WARN]') || 
                    line.includes('[INFO]') || line.includes('[DEBUG]')) {
                    processedContent += this.formatLogLine(line) + '\n';
                } else {
                    processedContent += this.escapeHtml(line) + '\n';
                }
            }
    
            // 更新显示
            const logsDisplay = document.getElementById('logs-display');
            if (logsDisplay) {
                logsDisplay.innerHTML = processedContent;
                // 保持滚动位置在底部
                logsDisplay.scrollTop = logsDisplay.scrollHeight;
            }
    
            // 处理下一块
            if (chunkIndex + 1 < totalChunks) {
                requestIdleCallback(() => processChunk(chunkIndex + 1));
            }
        };
    
        // 开始处理第一块
        requestIdleCallback(() => processChunk(0));
    
        return '处理中...'; // 初始显示
    },
    
    // 添加渲染可见行的方法
    renderVisibleLines(container, scrollTop) {
        if (!this._logLines) return;
    
        const lineHeight = 20; // 每行高度
        const containerHeight = container.clientHeight;
    
        // 计算可见范围
        const startIndex = Math.floor(scrollTop / lineHeight);
        const visibleLines = Math.ceil(containerHeight / lineHeight);
        const endIndex = Math.min(startIndex + visibleLines + 10, this._logLines.length); // 多渲染10行作为缓冲
    
        const viewport = container.querySelector('.log-content-viewport');
        if (!viewport) return;
    
        // 生成可见行的HTML
        const visibleContent = this._logLines
            .slice(startIndex, endIndex)
            .map((line, index) => {
                const absoluteIndex = startIndex + index;
                const formattedLine = line.includes('[ERROR]') || line.includes('[WARN]') || 
                    line.includes('[INFO]') || line.includes('[DEBUG]') 
                    ? this.formatLogLine(line) 
                    : this.escapeHtml(line);
                
                return `<div class="log-line" style="position: absolute; top: ${absoluteIndex * lineHeight}px;">${formattedLine}</div>`;
            })
            .join('');
        
        viewport.innerHTML = visibleContent;
    },
    

    
    // 添加单行日志格式化方法
    // 优化日志行渲染
    formatLogLine(line) {
    // 缓存正则表达式和图标映射
    if (!this._logLevelRegex) {
        this._logLevelRegex = /\[(ERROR|WARN|INFO|DEBUG)\]/;
        this._iconMap = {
            'error': 'error',
            'warn': 'warning',
            'info': 'info',
            'debug': 'code'
        };
    }
    
    const match = line.match(this._logLevelRegex);
    if (!match) return this.escapeHtml(line);
    
    const level = match[1];
    const levelClass = level.toLowerCase();
    const icon = `<span class="material-symbols-rounded">${this._iconMap[levelClass]}</span>`;
    
    // 修改标签结构以改善对齐
    return `<span class="log-line-content"><span class="log-level ${levelClass}">${icon}${level}</span>${this.escapeHtml(line.replace(this._logLevelRegex, ''))}</span>`;
},
    
    // 渲染页面
    render() {
        // 设置页面标题
        document.getElementById('page-title').textContent = I18n.translate('NAV_LOGS', '日志');
        
        // 添加操作按钮
        const pageActions = document.getElementById('page-actions');
        pageActions.innerHTML = `
            <button id="refresh-logs" class="icon-button" title="${I18n.translate('REFRESH_LOGS', '刷新日志')}">
                <span class="material-symbols-rounded">refresh</span>
            </button>
            <button id="export-logs" class="icon-button" title="${I18n.translate('EXPORT_LOGS', '导出日志')}">
                <span class="material-symbols-rounded">download</span>
            </button>
            <button id="clear-logs" class="icon-button" title="${I18n.translate('CLEAR_LOGS', '清除日志')}">
                <span class="material-symbols-rounded">delete</span>
            </button>
        `;
        
        const hasLogFiles = Object.keys(this.logFiles).length > 0;
        
        return `
            <div class="logs-container">
                <div class="card">
                    <div class="card-header">
                        <div class="controls-row">
                            <label>
                                <span>${I18n.translate('SELECT_LOG_FILE', '选择日志文件')}</span>
                                <select id="log-file-select" ${!hasLogFiles ? 'disabled' : ''}>
                                    ${this.renderLogFileOptions()}
                                </select>
                            </label>
                        </div>
                    </div>
                    
                    <div id="logs-display-container" class="card-content">
                        <div class="logs-scroll-container">
                            <pre id="logs-display" class="logs-content">${this.formatLogContent()}</pre>
                        </div>
                    </div>
                </div>
            </div>
        `;
    },
    
    // 渲染日志文件选项
    renderLogFileOptions() {
        if (Object.keys(this.logFiles).length === 0) {
            return `<option value="" disabled>${I18n.translate('NO_LOGS_FILES', '没有可用的日志文件')}</option>`;
        }
        
        return Object.keys(this.logFiles).map(fileName => 
            `<option value="${fileName}" ${this.currentLogFile === fileName ? 'selected' : ''}>${fileName}</option>`
        ).join('');
    },
    
    // 渲染后的回调
    afterRender() {
        // 日志文件选择器事件
        document.getElementById('log-file-select')?.addEventListener('change', (e) => {
            this.currentLogFile = e.target.value;
            this.loadLogContent(true);
        });
        
        // 刷新按钮事件
        document.getElementById('refresh-logs')?.addEventListener('click', () => {
            this.loadLogContent(true);
        });
        
        // 自动刷新切换事件
        document.getElementById('auto-refresh-checkbox')?.addEventListener('change', (e) => {
            this.toggleAutoRefresh(e.target.checked);
        });
        
        // 清除日志按钮事件
        document.getElementById('clear-logs')?.addEventListener('click', () => {
            this.clearLog();
        });
        
        // 导出日志按钮事件
        document.getElementById('export-logs')?.addEventListener('click', () => {
            this.exportLog();
        });
        
        // 如果设置了自动刷新，启动自动刷新
        if (this.autoRefresh) {
            this.toggleAutoRefresh(true);
        }
        
        // 添加日志显示区域的样式
        const logsDisplay = document.getElementById('logs-display');
        if (logsDisplay) {
            // 检测是否为空内容
            if (this.logContent.trim() === '') {
                logsDisplay.classList.add('empty');
            } else {
                logsDisplay.classList.remove('empty');
            }
        }
        
        // 设置日志容器高度
        this.adjustLogContainerHeight();
        
        // 监听窗口大小变化
        window.addEventListener('resize', this.adjustLogContainerHeight);
    },
    
    // 调整日志容器高度
    adjustLogContainerHeight() {
        const container = document.getElementById('logs-display-container');
        if (container) {
            const viewportHeight = window.innerHeight;
            const headerHeight = document.querySelector('header')?.offsetHeight || 0;
            const navHeight = document.querySelector('.app-nav')?.offsetHeight || 0;
            const controlsHeight = document.querySelector('.controls-row')?.offsetHeight || 0;
            
            // 计算可用高度
            const availableHeight = viewportHeight - headerHeight - navHeight - controlsHeight - 40; // 40px为其他边距
            
            // 设置最小高度
            container.style.minHeight = `${Math.max(300, availableHeight)}px`;
        }
    },
    
    // 页面激活/停用回调
    onActivate() { 
        // 刷新日志文件列表
        this.scanLogFiles().then(() => {
            // 如果当前选择的日志文件不在列表中，选择第一个
            if (this.currentLogFile && !this.logFiles[this.currentLogFile] && Object.keys(this.logFiles).length > 0) {
                this.currentLogFile = Object.keys(this.logFiles)[0];
            }
            // 加载日志内容
            this.loadLogContent();
            // 启动自动刷新
            this.autoRefresh && this.toggleAutoRefresh(true);
            // 调整容器高度
            this.adjustLogContainerHeight();
        });
        
        // 添加窗口大小变化监听
        window.addEventListener('resize', this.adjustLogContainerHeight);
    },
    
    onDeactivate() { 
        this.toggleAutoRefresh(false);
        // 移除窗口大小变化监听
        window.removeEventListener('resize', this.adjustLogContainerHeight);
    }
};

window.LogsPage = LogsPage;