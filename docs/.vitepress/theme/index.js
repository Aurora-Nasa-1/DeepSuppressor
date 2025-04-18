/**
 * AMMF2 VitePress 主题增强
 * 基于Material Design 3设计系统
 */

import DefaultTheme from 'vitepress/theme'
import './md3.css'
import './style.css'

// 页面过渡动画系统
const setupPageTransitions = () => {
  if (typeof window === 'undefined') return

  const content = document.querySelector('.VPContent')
  if (!content) return

  // 添加过渡类
  content.classList.add('page-transition')

  // 设置基础样式
  const style = document.createElement('style')
  style.textContent = `
    .page-transition {
      transition: opacity 0.3s ease-out, transform 0.3s ease-out;
    }
    .page-enter {
      opacity: 0;
      transform: translateY(10px);
    }
    .page-leave {
      opacity: 0;
      transform: translateY(-10px);
    }
  `
  document.head.appendChild(style)
}

// 平滑滚动增强
const setupSmoothScroll = () => {
  if (typeof window === 'undefined') return

  document.querySelectorAll('a[href^="#"]').forEach(anchor => {
    anchor.addEventListener('click', (e) => {
      e.preventDefault()
      const target = document.querySelector(anchor.getAttribute('href'))
      if (target) {
        target.scrollIntoView({
          behavior: 'smooth',
          block: 'start'
        })
      }
    })
  })
}

// 响应式导航栏
const setupResponsiveNavbar = () => {
  if (typeof window === 'undefined') return

  const nav = document.querySelector('.VPNav')
  if (!nav) return

  let lastScroll = window.scrollY
  let scrollTimer = null

  const handleScroll = () => {
    if (scrollTimer) return

    scrollTimer = window.requestAnimationFrame(() => {
      const currentScroll = window.scrollY
      const scrollDelta = currentScroll - lastScroll

      // 智能导航栏显示/隐藏逻辑
      if (scrollDelta > 0 && currentScroll > 100) {
        nav.style.transform = 'translateY(-100%)'
        nav.style.transition = 'transform 0.3s ease-out'
      } else {
        nav.style.transform = 'translateY(0)'
      }

      lastScroll = currentScroll
      scrollTimer = null
    })
  }

  window.addEventListener('scroll', handleScroll, { passive: true })
}

// MD3交互效果
const setupMD3Interactions = () => {
  if (typeof window === 'undefined') return

  // 按钮涟漪效果
  document.querySelectorAll('.VPButton').forEach(button => {
    button.addEventListener('click', (e) => {
      const rect = button.getBoundingClientRect()
      const ripple = document.createElement('div')
      
      ripple.className = 'md3-ripple'
      ripple.style.left = `${e.clientX - rect.left}px`
      ripple.style.top = `${e.clientY - rect.top}px`
      
      button.appendChild(ripple)
      ripple.addEventListener('animationend', () => ripple.remove())
    })
  })

  // 添加涟漪效果样式
  const style = document.createElement('style')
  style.textContent = `
    .md3-ripple {
      position: absolute;
      border-radius: 50%;
      transform: scale(0);
      animation: md3-ripple 0.6s linear;
      background-color: rgba(var(--vp-c-text-1), 0.1);
      pointer-events: none;
      width: 100px;
      height: 100px;
      margin: -50px;
    }
    @keyframes md3-ripple {
      to {
        transform: scale(4);
        opacity: 0;
      }
    }
  `
  document.head.appendChild(style)
}

// 主题配置
export default {
  extends: DefaultTheme,
  enhanceApp({ app, router }) {
    if (typeof window === 'undefined') return

    // 初始化主题增强功能
    router.onBeforeRouteChange = () => {
      const content = document.querySelector('.VPContent')
      if (content) {
        content.classList.add('page-leave')
      }
    }

    router.onAfterRouteChanged = () => {
      const content = document.querySelector('.VPContent')
      if (content) {
        content.classList.remove('page-leave')
        content.classList.add('page-enter')
        requestAnimationFrame(() => {
          content.classList.remove('page-enter')
        })
      }
    }

    // DOM加载完成后初始化功能
    window.addEventListener('DOMContentLoaded', () => {
      setupPageTransitions()
      setupSmoothScroll()
      setupResponsiveNavbar()
      setupMD3Interactions()
    })
  }
}