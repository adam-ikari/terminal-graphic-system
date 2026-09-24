# TGS 官网（Docusaurus）

`terminal-graphic-system` 的项目官网。内容为中文（默认 locale `zh-Hans`），
生产地址 `https://adam-ikari.github.io/terminal-graphic-system/`。

## 命令

```bash
npm start      # 本地开发 → http://localhost:3000/terminal-graphic-system/
npm run build  # 生产构建 → build/
npm run serve  # 预览构建产物
```

## 内容准则

- **数字同源**：页面上所有实测数字与 `docs/VERIFY-REPORT.html`、
  `protocol/tgs-spec-layer0.md` 保持一致；上游报告更新时同步。
- **诚实原则**：真实上限、已知缺口、环境限制如实标注，不美化。
- 截图证据从 `docs/screenshots/` 复制到 `static/img/screenshots/`，
  源图更新时重新复制。

## 结构

- `docs/` — 文档：简介 / 快速开始 / 架构 / 协议 / kitty 兼容面 / 验证报告 / 路线
- `src/pages/index.js` — 首页
- `src/components/HomepageFeatures/` — 首页特性卡
- `static/img/` — logo、favicon、证据截图
