// @ts-check
// TGS 官网配置 — https://adam-ikari.github.io/terminal-graphic-system/
import {themes as prismThemes} from 'prism-react-renderer';

/** @type {import('@docusaurus/types').Config} */
const config = {
  title: 'TGS 终端图形系统',
  tagline: '终端里的图形 API：程序 ⇄ 渲染器点对点，3 个绘制原语，TGS ⊃ kitty',
  favicon: 'img/favicon.svg',

  // Future flags, see https://docusaurus.io/docs/api/docusaurus-config#future
  future: {
    v4: true,
  },

  url: 'https://adam-ikari.github.io',
  baseUrl: '/terminal-graphic-system/',
  organizationName: 'adam-ikari',
  projectName: 'terminal-graphic-system',

  onBrokenLinks: 'throw',

  i18n: {
    defaultLocale: 'zh-Hans',
    locales: ['zh-Hans'],
  },

  presets: [
    [
      'classic',
      /** @type {import('@docusaurus/preset-classic').Options} */
      ({
        docs: {
          sidebarPath: './sidebars.js',
          editUrl:
            'https://github.com/adam-ikari/terminal-graphic-system/tree/main/site/',
        },
        blog: false,
        theme: {
          customCss: './src/css/custom.css',
        },
      }),
    ],
  ],

  themeConfig:
    /** @type {import('@docusaurus/preset-classic').ThemeConfig} */
    ({
      colorMode: {
        respectPrefersColorScheme: true,
      },
      navbar: {
        title: 'TGS',
        logo: {
          alt: 'TGS 标志',
          src: 'img/logo.svg',
        },
        items: [
          {
            type: 'docSidebar',
            sidebarId: 'docs',
            position: 'left',
            label: '文档',
          },
          {
            to: '/docs/verification',
            label: '验证',
            position: 'left',
          },
          {
            href: 'https://github.com/adam-ikari/terminal-graphic-system',
            label: 'GitHub',
            position: 'right',
          },
        ],
      },
      footer: {
        style: 'dark',
        links: [
          {
            title: '文档',
            items: [
              {label: '简介', to: '/docs/intro'},
              {label: '快速开始', to: '/docs/getting-started'},
              {label: '协议规范', to: '/docs/protocol'},
            ],
          },
          {
            title: '项目',
            items: [
              {label: '架构', to: '/docs/architecture'},
              {label: 'kitty 兼容面', to: '/docs/kitty-compat'},
              {label: '验证报告', to: '/docs/verification'},
              {label: '路线', to: '/docs/roadmap'},
            ],
          },
          {
            title: '仓库',
            items: [
              {
                label: 'GitHub',
                href: 'https://github.com/adam-ikari/terminal-graphic-system',
              },
              {
                label: 'L0 协议规范',
                href: 'https://github.com/adam-ikari/terminal-graphic-system/blob/main/protocol/tgs-spec-layer0.md',
              },
              {
                label: '验证报告（原文）',
                href: 'https://github.com/adam-ikari/terminal-graphic-system/blob/main/docs/VERIFY-REPORT.html',
              },
            ],
          },
        ],
        copyright: `TGS 终端图形系统 · 内容与数字与仓库 docs/、protocol/ 同源 · 由 Docusaurus 构建`,
      },
      prism: {
        theme: prismThemes.github,
        darkTheme: prismThemes.dracula,
      },
    }),
};

export default config;
