# VitePress 文档本地构建说明

本项目使用 VitePress 构建文档站点，以下是在本地构建和预览文档的步骤。

## 环境要求

- Node.js 16.0.0 或更高版本
- npm 或 yarn 包管理器

## 本地开发与预览

### 自动部署

本地自动部署，运行build_docs.sh脚本
```bash
build_docs.sh
```
### 手动部署

### 1. 安装依赖

在项目的 `docs` 目录下运行：

```bash
npm install
```

或者使用 yarn：

```bash
yarn
```

### 2. 启动开发服务器

```bash
npm run docs:dev
```

或者使用 yarn：

```bash
yarn docs:dev
```

启动后，可以在浏览器中访问 `http://localhost:5173` 查看文档站点。开发服务器支持热重载，当你修改文档内容时，页面会自动更新。

### 3. 构建静态文件

```bash
npm run docs:build
```

或者使用 yarn：

```bash
yarn docs:build
```

构建后的静态文件位于 `.vitepress/dist` 目录下。

### 4. 预览构建结果

```bash
npm run docs:preview
```

或者使用 yarn：

```bash
yarn docs:preview
```

## 文档结构

- `docs/` - 文档根目录
  - `.vitepress/` - VitePress 配置目录
    - `config.js` - 主要配置文件
    - `theme/` - 自定义主题文件
  - `*.md` - 中文文档文件
  - `en/*.md` - 英文文档文件

## 部署到 GitHub Pages

本项目已配置 GitHub Actions 工作流，当推送到 `main` 分支时，会自动构建文档并部署到 GitHub Pages。

你也可以手动触发工作流进行部署：

1. 在 GitHub 仓库页面，点击 "Actions" 标签
2. 在左侧菜单中选择 "Deploy VitePress to GitHub Pages" 工作流
3. 点击 "Run workflow" 按钮，选择 `main` 分支，然后点击 "Run workflow" 确认

## 注意事项

- 确保所有文档文件使用 Markdown 格式 (`.md` 文件)
- 图片等静态资源应放在 `public` 目录下
- 修改配置后需要重启开发服务器才能生效