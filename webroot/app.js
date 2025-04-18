/**
 * AMMF WebUI 主应用程序
 * 负责页面路由、UI管理和应用状态
 */

// 应用程序主类
class App {
    constructor() {
        this.state = {
            isLoading: true,
            currentPage: null,
            themeChanging: false,
            headerTransparent: true  // 添加此行
        };
    }

    // 初始化应用
    async init() {
        await I18n.init();
        ThemeManager.init();
        
        // 初始化CSS加载器
        if (window.CSSLoader) {
            CSSLoader.init();
        }

        // 检测是否为MMRL环境并初始化
        const isMMRLEnvironment = !!window.MMRL;
        if (isMMRLEnvironment) {
            MMRL.init();
            // 降低顶栏高度以适应手机状态栏
            document.body.classList.add('mmrl-environment');
        }
        
        // 添加应用加载完成标记
        requestAnimationFrame(() => document.body.classList.add('app-loaded'));

        // 初始化语言选择器
        if (window.I18n?.initLanguageSelector) {
            I18n.initLanguageSelector();
        } else {
            document.addEventListener('i18nReady', () => I18n.initLanguageSelector());
        }
        
        // 加载初始页面
        const initialPage = Router.getCurrentPage();
        await Router.navigate(initialPage, false);

        // 更新加载状态
        this.state.isLoading = false;

        // 移除加载指示器
        const loadingContainer = document.querySelector('#main-content .loading-container');
        if (loadingContainer) {
            loadingContainer.style.opacity = '0';
            setTimeout(() => loadingContainer.remove(), 300);
        }

        // 预加载其他页面
        Router.preloadPages();
    }

    // 执行命令
    async execCommand(command) {
        return await Core.execCommand(command);
    }
}

// 路由管理器
class Router {
    // 页面模块映射
    static modules = {
        status: 'StatusPage',
        logs: 'LogsPage',
        settings: 'SettingsPage',
        about: 'AboutPage'
    };

    // 页面缓存
    static cache = new Map();

    // 获取当前页面
    static getCurrentPage() {
        const hash = window.location.hash.slice(1);
        return this.modules[hash] ? hash : 'status';
    }

    // 预加载所有页面
    static preloadPages() {
        requestIdleCallback(() => {
            const pageOrder = ['status', 'settings', 'logs', 'about'];
            const currentPage = app.state.currentPage;

            // 移除当前页面
            const pagesToLoad = pageOrder.filter(page => page !== currentPage);

            // 并行预加载页面
            Promise.all(pagesToLoad.map(async (pageName) => {
                if (this.cache.has(pageName)) return;

                try {
                    const pageModule = window[this.modules[pageName]];
                    if (pageModule && typeof pageModule.init === 'function') {
                        await pageModule.init();
                        this.cache.set(pageName, pageModule);
                    }
                } catch (error) {
                    console.warn(`预加载页面 ${pageName} 失败:`, error);
                }
            }));
        }, { timeout: 2000 });
    }

    // 导航到指定页面
    static async navigate(pageName, updateHistory = true) {
        try {
            // 如果是当前页面，不进行切换
            if (app.state.currentPage === pageName) return;

            // 提前更新UI，提高感知速度
            UI.updateNavigation(pageName);
            UI.updatePageTitle(pageName);

            // 如果有当前页面，调用其 onDeactivate 方法
            if (app.state.currentPage) {
                const currentPageModule = this.cache.get(app.state.currentPage) ||
                    window[this.modules[app.state.currentPage]];
                if (currentPageModule?.onDeactivate) {
                    currentPageModule.onDeactivate();
                }
            }

            // 使用缓存或加载页面模块
            const pageModule = this.cache.get(pageName) || window[this.modules[pageName]];
            if (!pageModule) throw new Error(`页面模块 ${pageName} 未找到`);

            // 初始化页面模块（如果尚未初始化）
            if (!this.cache.has(pageName) && typeof pageModule.init === 'function') {
                const initResult = await pageModule.init();
                if (initResult === false) {
                    throw new Error(`页面 ${pageName} 初始化失败`);
                }
                this.cache.set(pageName, pageModule);
            }

            // 渲染页面内容
            const pageContent = pageModule.render();

            // 创建新页面容器
            const newContainer = document.createElement('div');
            newContainer.className = 'page-container';
            newContainer.innerHTML = pageContent;

            // 获取旧页面容器
            const oldContainer = document.querySelector('.page-container');

            // 执行页面过渡
            await this.performPageTransition(newContainer, oldContainer);

            // 执行页面渲染后的回调
            if (pageModule.afterRender) {
                pageModule.afterRender();
            }

            // 调用新页面的 onActivate 方法
            if (pageModule.onActivate) {
                pageModule.onActivate();
            }

            // 更新历史记录
            if (updateHistory) {
                window.history.pushState(null, '', `#${pageName}`);
            }

            app.state.currentPage = pageName;

        } catch (error) {
            console.error('页面导航错误:', error);
            UI.showError('页面加载失败', error.message);
        }
    }

    // 页面过渡效果 - 简化为纯淡入淡出
    static async performPageTransition(newContainer, oldContainer) {
        return new Promise(resolve => {
            if (oldContainer) {
                // 先添加新容器但设为透明
                UI.elements.mainContent.appendChild(newContainer);
                newContainer.style.opacity = '0';

                // 添加淡出动画类
                oldContainer.classList.add('fade-out');

                // 监听动画结束
                const onAnimationEnd = () => {
                    oldContainer.removeEventListener('animationend', onAnimationEnd);
                    oldContainer.remove();

                    // 显示新容器
                    newContainer.style.opacity = '1';

                    resolve();
                };

                oldContainer.addEventListener('animationend', onAnimationEnd);

                // 添加超时保护 (60ms动画 + 10ms缓冲)
                setTimeout(onAnimationEnd, 70);
            } else {
                // 没有旧容器，直接添加新容器
                UI.elements.mainContent.appendChild(newContainer);
                resolve();
            }
        });
    }
}

// UI管理器
class UI {
    // UI元素引用
    static elements = {
        app: document.getElementById('app'),
        header: document.querySelector('.app-header'),
        mainContent: document.getElementById('main-content'),
        navContent: document.querySelector('.nav-content'),
        pageTitle: document.getElementById('page-title'),
        pageActions: document.getElementById('page-actions'),
        themeToggle: document.getElementById('theme-toggle'),
        languageButton: document.getElementById('language-button'),
        toastContainer: document.getElementById('toast-container')
    };

    // 更新页面标题
    static updatePageTitle(pageName) {
        const titles = {
            status: I18n.translate('NAV_STATUS', '状态'),
            logs: I18n.translate('NAV_LOGS', '日志'),
            settings: I18n.translate('NAV_SETTINGS', '设置'),
            about: I18n.translate('NAV_ABOUT', '关于')
        };

        this.elements.pageTitle.textContent = titles[pageName] || 'AMMF WebUI';
    }

    // 显示底栏覆盖层 - 添加动画效果
    // 显示底栏覆盖层 - 添加动画效果
    static showOverlay(overlayElement) {
        if (!overlayElement) return;
        
        // 移除可能存在的closing类
        overlayElement.classList.remove('closing');
        
        // 先设置为显示
        overlayElement.style.display = 'flex';  // 改为flex以确保居中
        
        // 使用requestAnimationFrame确保DOM更新后再添加active类
        requestAnimationFrame(() => {
            overlayElement.classList.add('active');
        });
    }
    
    // 隐藏底栏覆盖层 - 添加动画效果
    static hideOverlay(overlayElement) {
        if (!overlayElement) return;
        
        // 添加closing类以触发淡出动画
        overlayElement.classList.add('closing');
        overlayElement.classList.remove('active');
        
        // 监听动画结束后隐藏元素
        const handleAnimationEnd = () => {
            overlayElement.removeEventListener('animationend', handleAnimationEnd);
            overlayElement.style.display = 'none';
            overlayElement.classList.remove('closing');
        };
        
        overlayElement.addEventListener('animationend', handleAnimationEnd);
        
        // 添加超时保护，确保元素最终被隐藏
        setTimeout(() => {
            if (!overlayElement.classList.contains('active')) {
                overlayElement.style.display = 'none';
                overlayElement.classList.remove('closing');
            }
        }, 300);
    }

    static toggleOverlay(overlayElement) {
        if (!overlayElement) return;

        if (!overlayElement.classList.contains('active')) {
            this.showOverlay(overlayElement);
        } else {
            this.hideOverlay(overlayElement);
        }
    }

    // 显示错误信息
    static showError(title, message) {
        this.elements.mainContent.innerHTML = `
            <div class="page-container">
                <div class="error-container">
                    <h2>${title}</h2>
                    <p>${message}</p>
                </div>
            </div>
        `;
    }

    // 更新导航状态
    static updateNavigation(pageName) {
        const navItems = document.querySelectorAll('.nav-item');
        navItems.forEach(item => {
        // 移除之前的动画类
        item.classList.remove('nav-entering', 'nav-exiting');
        
        if (item.dataset.page === pageName) {
            // 如果之前未激活,添加进入动画
            if (!item.classList.contains('active')) {
                item.classList.add('nav-entering');
            }
            item.classList.add('active');
        } else {
            // 如果之前已激活,添加退出动画
            if (item.classList.contains('active')) {
                item.classList.add('nav-exiting');
            }
            item.classList.remove('active');
        }
    });
    }

    static updateLayout() {
        const isLandscape = window.innerWidth >= 768;
        const header = this.elements.header;
        const pageActions = this.elements.pageActions;
        const themeToggle = this.elements.themeToggle;
        const languageButton = this.elements.languageButton;
    
        if (isLandscape) {
            // 横屏模式：移动按钮到侧栏
            const navContent = document.querySelector('.nav-content');
            if (navContent) {
                // 确保存在或创建操作按钮容器
                let pageActionsContainer = navContent.querySelector('.page-actions');
                if (!pageActionsContainer) {
                    pageActionsContainer = document.createElement('div');
                    pageActionsContainer.className = 'page-actions';
                    navContent.appendChild(pageActionsContainer);
                }
    
                // 确保存在或创建系统按钮容器
                let systemActionsContainer = navContent.querySelector('.system-actions');
                if (!systemActionsContainer) {
                    systemActionsContainer = document.createElement('div');
                    systemActionsContainer.className = 'system-actions';
                    navContent.appendChild(systemActionsContainer);
                }
    
                // 移动操作按钮
                if (pageActions && !pageActionsContainer.contains(pageActions)) {
                    pageActionsContainer.appendChild(pageActions);
                }
    
                // 移动系统按钮
                if (languageButton && !systemActionsContainer.contains(languageButton)) {
                    systemActionsContainer.appendChild(languageButton);
                }
                if (themeToggle && !systemActionsContainer.contains(themeToggle)) {
                    systemActionsContainer.appendChild(themeToggle);
                }
            }
        } else {
            // 竖屏模式：恢复按钮到顶栏
            const headerActions = header?.querySelector('.header-actions');
            if (headerActions) {
                if (pageActions && !headerActions.contains(pageActions)) {
                    headerActions.insertBefore(pageActions, headerActions.firstChild);
                }
                if (languageButton && !headerActions.contains(languageButton)) {
                    headerActions.appendChild(languageButton);
                }
                if (themeToggle && !headerActions.contains(themeToggle)) {
                    headerActions.appendChild(themeToggle);
                }
            }
        }
    }

    // 显示Toast通知
    static showToast(message, type = 'info', duration = 3000) {
        if (!this.elements.toastContainer) {
            this.elements.toastContainer = document.createElement('div');
            this.elements.toastContainer.id = 'toast-container';
            document.body.appendChild(this.elements.toastContainer);
        }

        // 创建toast元素
        const toast = document.createElement('div');
        toast.className = `toast toast-${type}`;

        // 创建内容容器
        const contentDiv = document.createElement('div');
        contentDiv.className = 'toast-content';

        // 添加消息文本
        const messageText = document.createElement('span');
        messageText.textContent = message;

        // 组装内容
        contentDiv.appendChild(messageText);
        toast.appendChild(contentDiv);

        // 添加到容器
        this.elements.toastContainer.appendChild(toast);

        // 使用requestAnimationFrame确保DOM更新后再添加动画类
        requestAnimationFrame(() => {
            toast.classList.add('show');
        });

        // 自动关闭
        const hideToast = () => {
            toast.classList.remove('show');
            toast.classList.add('hide');

            // 监听动画结束后移除元素
            const handleAnimationEnd = () => {
                toast.removeEventListener('animationend', handleAnimationEnd);
                if (this.elements.toastContainer?.contains(toast)) {
                    this.elements.toastContainer.removeChild(toast);
                }
            };

            toast.addEventListener('animationend', handleAnimationEnd);

            // 安全超时
            setTimeout(() => {
                if (this.elements.toastContainer?.contains(toast)) {
                    this.elements.toastContainer.removeChild(toast);
                }
            }, 300);
        };

        // 设置定时器
        const timer = setTimeout(hideToast, duration);

        // 添加交互功能：点击关闭
        toast.addEventListener('click', () => {
            clearTimeout(timer);
            hideToast();
        });

        return toast;
    }

    static updateHeaderTransparency() {
        const header = this.elements.header;
        const mainContent = this.elements.mainContent;
        const nav = document.querySelector('.app-nav');
        if (!header || !mainContent || !nav) return;
    
        const isLandscape = window.innerWidth >= 768;
        const scrollTop = mainContent.scrollTop;
        const headerHeight = header.offsetHeight;
        const contentHeight = mainContent.scrollHeight;
        const viewportHeight = mainContent.clientHeight;
        
        // 跟踪上次滚动位置
        if (!this.lastScrollPosition) {
            this.lastScrollPosition = scrollTop;
        }
        
        // 计算滚动方向 (1=向下, -1=向上)
        const scrollDirection = scrollTop > this.lastScrollPosition ? 1 : -1;
        this.lastScrollPosition = scrollTop;
        
        // 当滚动超过顶栏高度时显示背景
        if (scrollTop > headerHeight) {
            header.classList.add('header-solid');
            
            // 竖屏模式下根据滚动方向显示/隐藏底栏
            if (!isLandscape) {
                // 向下滚动时显示底栏
                if (scrollDirection === -1) {
                    nav.classList.remove('hidden');
                    nav.classList.add('visible');
                } 
                // 向上滚动时隐藏底栏
                else if (scrollDirection === 1 && scrollTop > headerHeight * 1.5) {
                    nav.classList.remove('visible');
                    nav.classList.add('hidden');
                }
            }
        } else {
            header.classList.remove('header-solid');
            // 顶部区域总是显示底栏
            nav.classList.remove('hidden');
            nav.classList.add('visible');
        }
        
        // 移除滚动到底部的检测逻辑
    }
}

// 初始化应用
const app = new App();

// 绑定事件监听器
window.addEventListener('DOMContentLoaded', () => {
    // 绑定导航项点击事件
    document.querySelectorAll('.nav-item').forEach(item => {
        item.addEventListener('click', () => {
            const page = item.dataset.page;
            if (page) Router.navigate(page);
        });
    });

    // 绑定主题切换按钮点击事件
    const themeToggle = document.getElementById('theme-toggle');
    if (themeToggle) {
        themeToggle.addEventListener('click', () => {
            if (window.ThemeManager?.toggleTheme) {
                window.ThemeManager.toggleTheme();
            }
        });
    }

    // 绑定语言切换按钮点击事件
    const languageButton = document.getElementById('language-button');
    if (languageButton) {
        languageButton.addEventListener('click', () => {
            const languageSelector = document.querySelector('.language-selector');
            if (languageSelector) {
                UI.showOverlay(languageSelector);
            }
        });
    }

    // 绑定取消语言选择按钮点击事件
    const cancelLanguage = document.getElementById('cancel-language');
    if (cancelLanguage) {
        cancelLanguage.addEventListener('click', () => {
            const languageSelector = document.querySelector('.language-selector');
            if (languageSelector) {
                UI.hideOverlay(languageSelector);
            }
        });
    }
    // 添加滚动监听
    const mainContent = document.getElementById('main-content');
    if (mainContent) {
        mainContent.addEventListener('scroll', () => {
            UI.updateHeaderTransparency();
        });
    }

    // 初始化顶栏状态
    UI.updateHeaderTransparency();
    // 初始化所有底栏覆盖层
    document.querySelectorAll('.bottom-overlay').forEach(overlay => {
        // 点击背景时隐藏
        overlay.addEventListener('click', (e) => {
            if (e.target === overlay) {
                UI.hideOverlay(overlay);
            }
        });

        // 点击关闭按钮时隐藏
        const closeButton = overlay.querySelector('.close-button');
        if (closeButton) {
            closeButton.addEventListener('click', () => {
                UI.hideOverlay(overlay);
            });
        }
    });

    // 绑定窗口大小变化事件
    window.addEventListener('resize', () => {
        UI.updateLayout();
    });

    // 绑定历史记录变化事件
    window.addEventListener('popstate', () => {
        const pageName = Router.getCurrentPage();
        Router.navigate(pageName, false);
    });

    // 初始化布局
    UI.updateLayout();

    // 初始化应用
    app.init().catch(error => {
        console.error('应用初始化失败:', error);
        UI.showError('应用初始化失败', error.message);
    });
});

// 导出全局API
window.app = {
    init: () => app.init(),
    execCommand: (command) => app.execCommand(command),
    loadPage: (pageName) => Router.navigate(pageName),
    o: (command) => app.execCommand(command)
};
