# VitePress Documentation Local Build Guide

This project uses VitePress to build the documentation site. Here are the steps to build and preview the documentation locally.

## Requirements

- Node.js 16.0.0 or higher
- npm or yarn package manager

## Local Development and Preview

### Automatic Deployment

For local automatic deployment, run the build_docs.sh script:
```bash
build_docs.sh
```
### Manual Deployment

### 1. Install Dependencies

In the `docs` directory of the project, run:

```bash
npm install
```

Or using yarn:

```bash
yarn
```

### 2. Start the Development Server

```bash
npm run docs:dev
```

Or using yarn:

```bash
yarn docs:dev
```

After starting, you can visit `http://localhost:5173` in your browser to view the documentation site. The development server supports hot reloading, so the page will automatically update when you modify the document content.

### 3. Build Static Files

```bash
npm run docs:build
```

Or using yarn:

```bash
yarn docs:build
```

The built static files are located in the `.vitepress/dist` directory.

### 4. Preview Build Results

```bash
npm run docs:preview
```

Or using yarn:

```bash
yarn docs:preview
```

## Documentation Structure

- `docs/` - Documentation root directory
  - `.vitepress/` - VitePress configuration directory
    - `config.js` - Main configuration file
    - `theme/` - Custom theme files
  - `*.md` - Chinese documentation files
  - `en/*.md` - English documentation files

## Deploy to GitHub Pages

This project has configured GitHub Actions workflow, which will automatically build the documentation and deploy it to GitHub Pages when pushed to the `main` branch.

You can also manually trigger the workflow for deployment:

1. On the GitHub repository page, click the "Actions" tab
2. Select "Deploy VitePress to GitHub Pages" workflow from the left menu
3. Click the "Run workflow" button, select the `main` branch, then click "Run workflow" to confirm

## Notes

- Ensure all documentation files use Markdown format (`.md` files)
- Static resources such as images should be placed in the `public` directory
- After modifying the configuration, you need to restart the development server for the changes to take effect