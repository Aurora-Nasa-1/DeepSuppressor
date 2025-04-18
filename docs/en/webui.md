# AMMF WebUI Development Guide

## 📋 Overview

This document provides development and customization guidelines for the WebUI part of the AMMF framework. WebUI is a browser-based configuration interface that allows users to configure module settings, view status information, and perform common operations through a graphical interface.

## 🚀 Quick Start

### File Structure

WebUI-related files are located in the `webroot/` directory:

```
webroot/
├── index.html         # Main page
├── app.js             # Application logic
├── core.js            # Core functionality
├── i18n.js            # Multi-language support
├── style.css          # Main stylesheet
├── theme.js           # Theme handling
├── css/               # Style modules
│   ├── animations.css # Animation effects
│   ├── app.css       # Base styles
│   ├── main-color.css # Theme color configuration
│   ├── pages.css     # Page styles
│   └── md3.css       # MD3 layout framework
└── pages/             # Page modules
    ├── status.js      # Status page
    ├── logs.js        # Logs page
    ├── settings.js    # Settings page
    └── about.js       # About page
```

## 🎨 Interface Development

### Style System

WebUI adopts the Material Design 3 design specification and uses a modular CSS structure:

- `style.css`: Main style file that imports other CSS modules
- `css/md3.css`: MD3 layout framework, providing base component styles
- `css/main-color.css`: Theme color configuration
- `css/app.css`: Application base styles
- `css/pages.css`: Page component styles
- `css/animations.css`: Animation effects

### Simple Configuration

The status page provides configuration options:

```javascript
        const quickActionsEnabled = false; // Set to false to hide all
        const quickActions = [
            {
                title: 'Clear Cache',
                icon: 'delete',
                command: 'rm -rf /data/local/tmp/*'
            },
            {
                title: 'Restart Service',
                icon: 'restart_alt',
                command: 'sh ${Core.MODULE_PATH}service.sh restart'
            },
            {
                title: 'View Logs',
                icon: 'description',
                command: 'cat ${Core.MODULE_PATH}logs.txt'
            }
        ];
```

The about page also provides simple configuration:

```javascript
    // Configuration options
    config: {
        showThemeToggle: false  // Control whether to show the theme toggle button
    },
```

### Custom Interface Styling

You can add custom CSS, loaded through css-loader.js:

```javascript
    // Custom CSS path
    customCSSPath: 'css/CustomCss/main.css',
```

**Note**:
- When using custom CSS, the original styles will be disabled.
- If you just want to override CSS, simply add styles or modify at the end of the original CSS file.

### Page Development

Each page is an independent JS module that needs to implement the following interfaces:

```javascript
const PageModule = {
    // Initialization function
    async init() {
        // Return initialization status
        return true;
    },
    
    // Render page content
    render() {
        return `
            <div class="page-container">
                <!-- Page content -->
            </div>
        `;
    },
    
    // Post-render processing
    afterRender() {
        // Bind events and other operations
    }
};
```

### Adding New Pages

1. Create a page module file in the `pages/` directory
2. Import the page script in `index.html`
3. Register the page module in `app.js`
4. Add navigation bar entry
5. Add related translations in `i18n.js`

## 🔄 Core Functionality

### Data Processing

WebUI communicates with the backend through the API provided by `core.js`:

```javascript
// Execute Shell command
async execCommand(command) {
    return new Promise((resolve, reject) => {
        const callbackName = `exec_callback_${Date.now()}`;
        window[callbackName] = (errno, stdout, stderr) => {
            delete window[callbackName];
            errno === 0 ? resolve(stdout) : reject(stderr);
        };
        ksu.exec(command, "{}", callbackName);
    });
}
```

### Multi-language Support

Implement multi-language support using `i18n.js`:

- Use `data-i18n` attribute to mark text that needs translation
- Add translation strings in `i18n.js`
- Call `I18n.translate()` to get translations

### Theme Support

Implement theme switching through `theme.js`:

- Use CSS variables to define theme colors
- Support light/dark modes
- Respond to system theme changes

## 📱 Responsive Design

WebUI adopts a mobile-first responsive design:

```css
/* Mobile devices */
@media (max-width: 767px) {
    .app-nav {
        height: 56px;
        bottom: 0;
    }
}

/* Tablet devices */
@media (min-width: 768px) and (max-width: 1023px) {
    .app-nav {
        height: 60px;
        bottom: 0;
    }
}

/* Desktop devices */
@media (min-width: 1024px) {
    .app-nav {
        width: 80px;
        height: 100%;
        flex-direction: column;
        left: 0;
    }
}
```

## 🔧 Development Tips

1. **Debugging Tools**
   - Use browser developer tools (F12)
   - Add `console.log()` to output debug information
   - Use `Core.showToast()` to display notifications

2. **Code Standards**
   - Follow Material Design 3 design specifications
   - Use modular CSS structure
   - Keep code clean and well-commented

3. **Performance Optimization**
   - Minimize unnecessary DOM operations
   - Optimize event listeners
   - Use CSS animations instead of JavaScript animations

## 🔄 Version Compatibility

When upgrading the AMMF framework, pay attention to changes in the following files:

- `app.js`: Application logic
- `core.js`: Core functionality
- `i18n.js`: Language configuration
- `css/`: Style files

It is recommended to back up customized files before upgrading, then carefully merge any changes.