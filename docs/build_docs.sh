#!/bin/bash
get_current_user() {
    if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "win32" ]]; then
        # Windows系统获取用户名
        echo "$USERNAME"  # Windows环境变量
    else
        # Linux/Mac系统获取用户名
        echo "$USER"
    fi
}

USER=$(get_current_user)
# VitePress文档本地构建脚本
# 此脚本用于自动构建VitePress文档并在退出时清理临时文件
[[ "$OSTYPE" == "msys" || "$OSTYPE" == "win32" ]] && IS_WINDOWS=1
# 颜色定义
GREEN="\033[0;32m"
YELLOW="\033[0;33m"
RED="\033[0;31m"
NC="\033[0m" # No Color

# 工作目录
DOCS_DIR="$(pwd)"
DIST_DIR="$DOCS_DIR/.vitepress/dist"
NODE_MODULES="$DOCS_DIR/node_modules"

# 清理函数
cleanup() {
    echo -e "\n${YELLOW}正在清理构建文件...${NC}"

    # 检查并删除dist目录
    if [ -d "$DIST_DIR" ]; then
        echo -e "${YELLOW}删除构建目录: $DIST_DIR${NC}"
        rm -rf "$DIST_DIR"
    fi
    if [ -d "$DOCS_DIR/.vitepress/cache" ]; then
        echo -e "${YELLOW}删除缓存目录: $DOCS_DIR/.vitepress/cache ${NC}"
        rm -rf "$DOCS_DIR/.vitepress/cache"
    fi
    if [ -d "$NODE_MODULES" ]; then
        # 移动依赖目录到用户目录
        if [ $IS_WINDOWS -eq 1 ]; then
            MODULES_BACKUP="/c/Users/$USER/node_modules_backup_$(basename "$DOCS_DIR")"
            mv "$DOCS_DIR/package-lock.json" "/c/Users/$USER/.JSONbackup_$(basename "$DOCS_DIR")"
        else
            MODULES_BACKUP="$HOME/.node_modules_backup_$(basename "$DOCS_DIR")"
            mv "$DOCS_DIR"/package-lock.json "$MODE/.JSONbackup_$(basename "$DOCS_DIR")"
        fi
        echo -e "${YELLOW}移动依赖目录到: $MODULES_BACKUP${NC}"
        mv "$NODE_MODULES" "$MODULES_BACKUP"
    fi
    echo -e "${GREEN}清理完成!${NC}"
    exit 0
}

# 设置信号处理，捕获Ctrl+C和其他终止信号
trap cleanup SIGINT SIGTERM EXIT

# 检查Node.js环境
if ! command -v node &>/dev/null; then
    echo -e "${RED}错误: 未找到Node.js${NC}"
    echo -e "${YELLOW}请安装Node.js 16.0.0或更高版本${NC}"
    exit 1
fi

# 检查npm
if ! command -v npm &>/dev/null; then
    echo -e "${RED}错误: 未找到npm${NC}"
    exit 1
fi

# 显示Node.js和npm版本
echo -e "${GREEN}Node.js版本:${NC} $(node -v)"
echo -e "${GREEN}npm版本:${NC} $(npm -v)"

# 进入docs目录
cd "$DOCS_DIR" || {
    echo -e "${RED}错误: 无法进入docs目录${NC}"
    exit 1
}

# 安装依赖
echo -e "\n${YELLOW}正在安装依赖...${NC}"
if [ ! -d "node_modules" ]; then
    # 检查是否有备份的依赖目录
    if [ $IS_WINDOWS -eq 1 ]; then
        MODULES_BACKUP="/c/Users/$USER/node_modules_backup_$(basename "$DOCS_DIR")"
        mv "/c/Users/$USER/.JSONbackup_$(basename "$DOCS_DIR")" "$DOCS_DIR/package-lock.json"
    else
        MODULES_BACKUP="$HOME/node_modules_backup_$(basename "$DOCS_DIR")"
        mv "$HOME/.JSONbackup_$(basename "$DOCS_DIR")" "$DOCS_DIR/package-lock.json"
    fi
    if [ -d "$MODULES_BACKUP" ]; then
        echo -e "${YELLOW}恢复备份的依赖目录...${NC}"
        mv "$MODULES_BACKUP" "$NODE_MODULES"
    else
        npm install || {
            echo -e "${RED}错误: 依赖安装失败${NC}"
            sleep 11
            exit 1
        }
    fi
else
    echo -e "${GREEN}依赖已存在，跳过安装${NC}"
fi

# 提供选项菜单
echo -e "\n${GREEN}VitePress文档工具${NC}"
echo -e "${YELLOW}1. 启动开发服务器${NC}"
echo -e "${YELLOW}2. 构建静态文件${NC}"
echo -e "${YELLOW}3. 预览构建结果${NC}"
echo -e "${YELLOW}0. 退出${NC}"
echo -e "\n${GREEN}请选择操作 [0-3]:${NC} "
read -r choice

case $choice in
1)
    # 启动开发服务器
    echo -e "\n${GREEN}启动VitePress开发服务器...${NC}"
    echo -e "${YELLOW}按Ctrl+C退出并清理构建文件${NC}\n"
    npm run docs:dev
    ;;
2)
    # 构建静态文件
    echo -e "\n${GREEN}构建VitePress静态文件...${NC}"
    npm run docs:build
    echo -e "\n${GREEN}构建完成! 静态文件位于: $DIST_DIR${NC}"
    echo -e "${YELLOW}按任意键退出并清理构建文件${NC}"
    read -n 1
    ;;
3)
    # 构建并预览
    echo -e "\n${GREEN}构建VitePress静态文件...${NC}"
    npm run docs:build
    echo -e "\n${GREEN}启动预览服务器...${NC}"
    echo -e "${YELLOW}按Ctrl+C退出并清理构建文件${NC}\n"
    npm run docs:preview
    ;;
0 | *)
    echo -e "${GREEN}退出程序${NC}"
    exit 0
    ;;
esac

# 注意：由于trap设置，脚本结束时会自动调用cleanup函数
# 所以这里不需要显式调用cleanup
