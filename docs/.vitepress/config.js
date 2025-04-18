export default {
  title: 'AMMF2 文档',
  description: 'Aurora Modular Magisk Framework 2 文档',
  lang: 'zh-CN',
  lastUpdated: true,
  base: process.env.NODE_ENV === 'production' ? '/AMMF2/' : '/',
  srcDir: './',
  
  themeConfig: {
    nav: [
      { text: '首页', link: '/' }
    ],
    ignoreDeadLinks: [
      /^\.\.\/\.\.\/LICENSE$/,
      /^\.\.\/README$/
    ],
    // 修改侧边栏配置，确保路径正确
    sidebar: {
      '/': [
        {
          text: '快速开始',
          collapsible: true,  // 添加可折叠选项
          items: [
            { text: '项目概述', link: '/README' },  // 改为指向README
            { text: '目录结构', link: '/directory' },
            { text: '文档界面本地构建说明', link: '/local-build-guide' },
          ]
        },
        {
          text: '开发指南',
          collapsible: true,
          items: [
            { text: '脚本指南', link: '/script' },
            { text: 'WebUI 指南', link: '/webui' },
          ]
        }
      ],
      '/en/': [
        {
          text: 'Quick Start',
          items: [
            { text: 'Overview', link: '/en/README' },
            { text: 'Directory Structure', link: '/en/directory' },
            { text: 'Docs Local Build Guide', link: '/en/local-build-guide' },
          ]
        },
        {
          text: 'Development Guide',
          items: [
            { text: 'Script Guide', link: '/en/script' },
            { text: 'WebUI Guide', link: '/en/webui' },
          ]
        }
      ]
    },
    
    // 社交链接
    socialLinks: [
      { icon: 'github', link: 'https://github.com/Aurora-Nasa-1/AMMF2' }
    ],
    
    // 页脚
    footer: {
      message: 'Released under the MIT License.',
      copyright: 'Copyright © 2025 AuroraNasa'
    },
    
    // 搜索
    search: {
      provider: 'local'
    }
  },
  
  // 多语言配置
  locales: {
    root: {
      label: '简体中文',
      lang: 'zh-CN',
    },
    en: {
      label: 'English',
      lang: 'en-US',
      link: '/en/'
    }
  }
}