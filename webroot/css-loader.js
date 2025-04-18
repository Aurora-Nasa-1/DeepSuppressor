/**
 * AMMF WebUI CSS加载器
 * 提供动态加载CSS文件的功能
 */

const CSSLoader = {
    // 默认CSS路径
    defaultCSSPath: 'css/style.css',
    
    // 自定义CSS路径
    customCSSPath: 'css/CustomCss/main.css',
    
    // 当前加载的CSS类型
    currentCSSType: 'default',
    
    // 已加载的CSS链接元素
    loadedCSSLink: null,
    
    // 初始化CSS加载器
    init() {
        // 从本地存储获取上次使用的CSS类型和色调值
        const savedCSSType = localStorage.getItem('ammf_css_type') || 'default';
        const savedHue = localStorage.getItem('ammf_color_hue') || '300';
        
        // 移除index.html中直接引用的CSS
        this.removeDirectCSSLinks();
        
        // 加载保存的CSS类型
        this.loadCSS(savedCSSType);
        
        // 应用保存的色调值
        document.documentElement.style.setProperty('--hue', savedHue);
        
        // 监听主题变更事件，确保CSS与主题兼容
        document.addEventListener('themeChanged', () => {
        });
        
        // 监听颜色变化事件
        document.addEventListener('colorChanged', (e) => {
            const hue = e.detail.hue;
            document.documentElement.style.setProperty('--hue', hue);
            localStorage.setItem('ammf_color_hue', hue);
        });
        
        console.log('CSS加载器初始化完成，当前CSS类型:', savedCSSType, '当前色调:', savedHue);
    },
    
    // 移除直接在HTML中引用的CSS链接
    removeDirectCSSLinks() {
        const styleLink = document.querySelector('link[href="style.css"]');
        if (styleLink) {
            styleLink.remove();
            console.log('已移除直接引用的style.css');
        }
    },
    
    // 加载指定类型的CSS
    loadCSS(cssType) {
        // 保存CSS类型
        this.currentCSSType = cssType;
        localStorage.setItem('ammf_css_type', cssType);
        
        // 确定要加载的CSS路径
        const cssPath = cssType === 'custom' ? this.customCSSPath : this.defaultCSSPath;
        
        // 如果已有加载的CSS，先移除
        if (this.loadedCSSLink) {
            this.loadedCSSLink.remove();
        }
        
        // 创建新的链接元素
        const link = document.createElement('link');
        link.rel = 'stylesheet';
        link.href = cssPath;
        
        // 添加到文档头部
        document.head.appendChild(link);
        this.loadedCSSLink = link;
        
        console.log(`已加载${cssType === 'custom' ? '自定义' : '默认'}CSS: ${cssPath}`);
        
        // 触发CSS加载事件
        document.dispatchEvent(new CustomEvent('cssLoaded', { 
            detail: { type: cssType, path: cssPath }
        }));
        
        return true;
    },
    
    // 切换CSS类型
    toggleCSS() {
        const newType = this.currentCSSType === 'custom' ? 'default' : 'custom';
        return this.loadCSS(newType);
    },
    
    // 获取当前CSS类型
    getCurrentCSSType() {
        return this.currentCSSType;
    }
};

// 导出CSS加载器
window.CSSLoader = CSSLoader;