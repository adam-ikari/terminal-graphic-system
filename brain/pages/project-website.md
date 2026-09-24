---
id: project-website
title: "TGS 官方网站（Docusaurus）"
category: project
status: active
tags: [website, docs]
created: "2026-09-24T06:09:25"
updated: "2026-09-24T10:23:41"
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

- time: 2026-09-24T09:02:33
  kind: decision
  summary: "上线路径定为 GitHub Actions → GitHub Pages（build_type=workflow）：push main 且改动命中 site/** 时自动 npm ci + build，产物 site/build 作为 Pages artifact 发布；地址 https://adam-ikari.github.io/terminal-graphic-system/。远端 origin 此前为空仓库，首推为全历史。"
  source: deploy
  affects: [project-website]

- time: 2026-09-24T10:23:41
  kind: evidence
  summary: "官网上线（2026-09-24）：https://adam-ikari.github.io/terminal-graphic-system/ HTTP200，8 路由验证，部署链 = push main 命中 site/** → Actions（npm ci+build → Pages artifact）。远端空仓库首推完成（main=a235494）。过程中钉死三个环境约束：①本地上行 ~400KB/s，单次 git push >~30MB 必 HTTP408（37MB 历史两次失败）→ 用 bundle 切 5MB 分片借 relay 分支上行、由 Actions 运行器重组后服务侧推 main；②Actions workflow 必须存在于默认分支才会被注册/触发；③GITHUB_TOKEN 无 workflows 权限不能创建 workflow 文件（含 workflows:write 键非法、0s 校验失败）→ 无 workflow 的历史用 GITHUB_TOKEN 推，site.yml 尾提交用带 workflow scope 的 PAT 推；另：git2.34 bundle create 对裸 SHA/HEAD~1 报空、仅 ref 可用。"
  source: deploy
  affects: [project-website]
