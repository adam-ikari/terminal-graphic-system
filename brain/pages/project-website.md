---
id: project-website
title: "TGS 官方网站（Docusaurus）"
category: project
status: active
tags: [website, docs]
created: "2026-09-24T06:09:25"
updated: "2026-09-24T06:27:48"
---

<!-- compiled_truth -->
TGS 对外官网采用 Docusaurus（用户选定），内容为项目介绍 / 协议与特性 / 验证数据（把 VERIFY-REPORT 的实测结论 web 化）。仓库内独立目录 site/，默认 locale 为中文；构建产物与 node_modules 不入库。诚实原则延续：页面上的每个数字都必须与 docs/VERIFY-REPORT 同源，标注真实上限与已知缺口，不美化。


## Timeline

- time: 2026-09-24T06:09:25
  kind: decision
  summary: "Created this page: TGS 官方网站（Docusaurus）"
  source: user-decision
  affects: [project-website]

- time: 2026-09-24T06:09:26
  kind: decision
  summary: "官网用 Docusaurus，site/ 目录，中文内容，数字与 VERIFY-REPORT 同源"
  source: user-decision
  affects: [project-website]

- time: 2026-09-24T06:09:26
  kind: decision
  summary: "启动官网任务：Docusaurus classic 模板，site/ 目录；内容源为 docs/VERIFY-REPORT.md、docs/architecture-v2.md、protocol/tgs-spec-layer0.md"
  source: user-decision
  affects: [project-website]

- time: 2026-09-24T06:27:48
  kind: evidence
  summary: "site/ 落地：Docusaurus 3.10.2 classic 模板，zh-Hans 默认 locale；首页（hero+5 项数据条+4 特性卡+4 张证据截图）+ 7 文档页（简介/快速开始/架构/协议/kitty兼容/验证/路线）。npm run build 全绿（onBrokenLinks=throw 含 CJK 锚点校验）；8 路由 200；图片 baseUrl 与深度锚点（分层路线-l0--l6、验证抓出的真-bug）经产物 grep 验证。所有数字与 docs/VERIFY-REPORT.html、protocol spec 同源；演示模板内容与 Docusaurus 品牌资源已清除。"
  source: build
  affects: [project-website]
