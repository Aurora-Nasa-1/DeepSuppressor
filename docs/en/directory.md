# AMMF Directory Structure Explained

## 📂 Overview

This document describes in detail the directory structure of AMMF (Aurora Magisk Module Framework), helping developers understand the purpose of each file and directory for module development and customization.

## 🗂️ Root Directory Structure

```
/
├── .github/                # GitHub related configurations
│   ├── ISSUE_TEMPLATE/    # Issue templates
│   └── workflows/         # GitHub Action workflows
├── bin/                   # Binary tools
├── docs/                  # Documentation directory
├── files/                 # Module files
│   ├── languages.sh       # Multi-language support
│   └── scripts/           # Scripts directory
├── module_settings/       # Module settings
├── src/                   # Source code directory
├── webroot/               # WebUI files
├── action.sh              # User action script
├── customize.sh           # Installation customization script
├── LICENSE                # License file
└── service.sh             # Service script
```

## 📁 Detailed Directory Description

### .github/

Contains GitHub related configurations for automated builds and issue management.

- **ISSUE_TEMPLATE/**

  - `bug_report.yml` - Bug report template
  - `feature_request.yml` - Feature request template

- **workflows/**
  - `build_module_commit.yml` - Workflow for building module on commit
  - `build_module_release_tag.yml` - Workflow for building module on release tag

### bin/

Contains binary tools for special module functions.

- `filewatch` - File monitoring tool for watching file changes and triggering actions
- `logmonitor` - Log monitoring tool for managing module logs

### docs/

Contains project documentation, providing usage guides and development instructions.

- `README.md` - Chinese project description
- `README_EN.md` - English project description
- `SCRIPT.md` - Chinese script usage instructions
- `SCRIPT_EN.md` - English script usage instructions
- `WEBUI_GUIDE.md` - Chinese WebUI development guide
- `WEBUI_GUIDE_EN.md` - English WebUI development guide
- `DIRECTORY_STRUCTURE.md` - Chinese directory structure description
- `DIRECTORY_STRUCTURE_EN.md` - English directory structure description

### files/

Contains core files and scripts for the module.

- `languages.sh` - Multi-language support configuration file, defining text strings for various languages

- **scripts/**
  - `default_scripts/` - Default scripts directory
    - `main.sh` - Main functionality script, providing core functions and variables
  - `install_custom_script.sh` - Custom script executed during installation
  - `service_script.sh` - Script executed when service is running

### module_settings/

Contains configuration files for the module.

- `config.sh` - Basic module configuration, such as module ID, name, author, description, etc.
- `settings.json` - Settings JSON file used by WebUI

### src/

Contains source code files for the module, used to compile binary tools.

- `filewatch.cpp` - File monitoring tool source code
- `logmonitor.cpp` - Log monitoring tool source code

### webroot/

Contains WebUI related files for providing a graphical configuration interface.

- `index.html` - WebUI main page
- `app.js` - WebUI main application logic
- `core.js` - WebUI core functionality
- `i18n.js` - WebUI internationalization support
- `style.css` - WebUI stylesheet
- `theme.js` - WebUI theme handling
- **css/** - CSS style files directory
  - `animations.css` - Animation effect styles
  - `app.css` - Base styles
  - `page.css` - Page component styles
  - `md3.css` - MD3 layout styles (https://github.com/jogemu/md3css)
  - `main-color.css` - MD3 theme color configuration
- **pages/** - Page components directory
  - `about.js` - About page
  - `logs.js` - Logs page
  - `settings.js` - Settings page
  - `status.js` - Status page

## 📄 Core Files Description

### action.sh

User action script, used to execute specific operations after module installation. This script can be manually executed by users to trigger specific module functions.

```bash
#!/system/bin/sh
MODDIR=${0%/*}
MODPATH="$MODDIR"
    if [ ! -f "$MODPATH/files/scripts/default_scripts/main.sh" ]; then
        abort "Notfound File!!!($MODPATH/files/scripts/default_scripts/main.sh)"
    else
        . "$MODPATH/files/scripts/default_scripts/main.sh"
    fi
# Custom Script
# -----------------
# This script extends the functionality of the default and setup scripts, allowing direct use of their variables and functions.
```

### customize.sh

Installation customization script, executed during the Magisk module installation process. Responsible for initializing the module, checking version compatibility, replacing module ID, and other operations.
Not recommended to modify; use `install_custom_script.sh` for custom scripts.

### service.sh

Service script, executed by Magisk after system startup. Responsible for starting the module's background services and monitoring functions.
Not recommended to modify; use `service_script.sh` for custom scripts.

## 🔄 Version Compatibility

When upgrading the AMMF framework version, pay attention to changes in the following files:

1. `files/languages.sh` - Multi-language support may be updated
2. `files/scripts/default_scripts/main.sh` - Core functions may change
3. `module_settings/config.sh` - Configuration options may be added or removed
4. `webroot/` directory - WebUI related files may be updated

It is recommended to back up custom content before upgrading, then merge changes.