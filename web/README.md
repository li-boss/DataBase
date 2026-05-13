# Web 数据库可视化

## 项目信息

- **项目来源**: Wecom 接单
- **客户名称**: Peppestone
- **立项时间**: 20260422
- **项目名称**: Web 数据库可视化
- **当前版本**: 0.1.0
- **项目定位**: 面向 `RuankoDB` 的 Web 可视化管理端，提供数据库切换、建表、结构查看、数据浏览、行级增删改和 SQL 查询能力

## 技术栈

- **前端框架**: Vue 3
- **构建工具**: Vite 8
- **UI 组件库**: Ant Design Vue 4
- **语言**: JavaScript + Vue SFC
- **接口方式**: HTTP REST API
- **默认后端代理**: `http://127.0.0.1:8080`
- **构建产物**: `dist/`

## 功能范围

- 数据库列表读取与切换
- 数据表列表读取、创建、删除
- 表结构查看
- 表数据浏览
- 记录插入、更新、删除
- SQL 查询与命令执行
- 前端统一错误提示与接口结果归一化

## 项目结构

```text
Master/
├─ src/
│  ├─ api/              # 接口请求封装
│  ├─ components/       # 页面组件
│  ├─ styles/           # 全局样式
│  ├─ utils/            # 数据归一化工具
│  ├─ App.vue           # 主界面
│  └─ main.js           # 应用入口
├─ dist/                # 生产构建产物
├─ _dev/                # 设计文档与开发记录
├─ index.html           # Vite 页面入口
├─ package.json         # 依赖与脚本
└─ vite.config.js       # Vite 配置与 API 代理
```

## 接口约定

- 开发环境默认通过 Vite 代理 `/api` 到 `http://127.0.0.1:8080`
- 如需直连其他后端地址，可配置环境变量 `VITE_API_BASE`
- 当前前端已对以下接口做封装：
  - `/api/databases`
  - `/api/database`
  - `/api/use/{db}`
  - `/api/tables`
  - `/api/table`
  - `/api/schema/{table}`
  - `/api/data/{table}`
  - `/api/data/{table}/{row}`
  - `/api/query`

## 快速开始

### 1. 安装依赖

```bash
npm install
```

### 2. 启动开发环境

```bash
npm run dev
```

默认访问地址：

```text
http://localhost:5173
```

### 3. 生产构建

```bash
npm run build
```

### 4. 本地预览构建结果

```bash
npm run preview
```

## 开发说明

- 当前运行入口为根目录工程，不是旧的子目录脚手架
- `src/api/client.js` 负责请求封装、错误处理和后端响应解析
- `src/utils/normalizers.js` 负责接口字段兼容与表格数据标准化
- 当前 `package.json` 未配置自动化测试脚本，交付前以手工联调和构建校验为主

## 变更记录

### 2026-04-23

- 优化页面用户文案
- 调整 SQL 底部抽拉面板交互与收起态布局
- 修正 Windows 环境下 Vite 构建脚本的兼容问题

### 2026-04-22

- 初始化 Vite + Vue 3 + Ant Design Vue 前端工程
- 完成数据库导航、结构查看、数据表、SQL 操作区等核心界面
- 接入 RuankoDB REST API 并完成基础交互

## 联系方式

如有问题，请联系 `zhonfortune@outlook.com`
