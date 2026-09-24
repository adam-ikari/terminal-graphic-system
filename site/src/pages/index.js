import clsx from 'clsx';
import Link from '@docusaurus/Link';
import useDocusaurusContext from '@docusaurus/useDocusaurusContext';
import Layout from '@theme/Layout';
import Heading from '@theme/Heading';
import useBaseUrl from '@docusaurus/useBaseUrl';
import HomepageFeatures from '@site/src/components/HomepageFeatures';

import styles from './index.module.css';

const STATS = [
  {value: '67/67', label: 'ctest 单测全绿'},
  {value: '100%', label: '像素级往返（maxdiff=0）'},
  {value: '0 B/s', label: '空闲带宽（脏区传输）'},
  {value: '123.7fps', label: '@120Hz 锚点节拍'},
  {value: '3', label: '绘制原语（无控件协议）'},
];

const SHOTS = [
  {
    src: 'img/screenshots/container.png',
    alt: '容器组合：BOX 子树递归嵌套',
    caption: '容器组合——BOX 作父，子元素在程序算好的 rect 上',
  },
  {
    src: 'img/screenshots/states_clicked.png',
    alt: '状态重绘：点击态画面',
    caption: '状态重绘（点击态）——程序侧状态经 WGT_STYLE 驱动',
  },
  {
    src: 'img/screenshots/kitty_roundtrip.png',
    alt: 'kitty 帧像素级往返验证画面',
    caption: 'kitty 帧往返——pty 捕获 → 解码 → 与 ground truth 逐像素对比',
  },
  {
    src: 'img/screenshots/mixed.png',
    alt: '混合场景：多原语同屏',
    caption: '混合场景——多原语同屏绘制',
  },
];

function HomepageHeader() {
  const {siteConfig} = useDocusaurusContext();
  return (
    <header className={styles.heroBanner}>
      <div className="container">
        <Heading as="h1" className={styles.heroTitle}>
          TGS<span className={styles.cursor}>_</span>
        </Heading>
        <p className={styles.heroSubtitle}>{siteConfig.tagline}</p>
        <div className={styles.buttons}>
          <Link
            className="button button--primary button--lg"
            to="/docs/getting-started">
            快速开始
          </Link>
          <Link
            className="button button--outline button--primary button--lg"
            to="/docs/verification">
            验证数据
          </Link>
        </div>
        <p className={styles.dualId}>
          <code>ESC _ G1;… ESC \</code> TGS 帧
          <span className={styles.dualSep}>·</span>
          <code>ESC _ Gf=100,a=T,… ESC \</code> kitty 原生帧
          <span className={styles.dualSep}>·</span>同一 APC 通道，两端双识别
        </p>
      </div>
    </header>
  );
}

function StatsStrip() {
  return (
    <section className="container">
      <div className={styles.statsStrip}>
        {STATS.map((s) => (
          <div key={s.label} className={styles.statCard}>
            <span className={styles.statValue}>{s.value}</span>
            <span className={styles.statLabel}>{s.label}</span>
          </div>
        ))}
      </div>
    </section>
  );
}

function EvidenceShots() {
  const baseUrl = useBaseUrl('/');
  return (
    <section className="container">
      <div className={styles.shotsSection}>
        <Heading as="h2" className={styles.sectionTitle}>
          画面即证据
        </Heading>
        <p className={styles.sectionSub}>
          实拍截图源自仓库 <code>docs/screenshots/</code>；每一项声明的验证方法
          见 <Link to="/docs/verification">验证报告</Link>。
        </p>
        <div className={styles.shotGrid}>
          {SHOTS.map((shot) => (
            <figure key={shot.src} className={styles.shot}>
              <img src={baseUrl + shot.src} alt={shot.alt} />
              <figcaption className={styles.shotCaption}>
                {shot.caption}
              </figcaption>
            </figure>
          ))}
        </div>
      </div>
    </section>
  );
}

export default function Home() {
  const {siteConfig} = useDocusaurusContext();
  return (
    <Layout
      title="终端里的图形 API"
      description="TGS——终端里的图形 API。程序 ⇄ 渲染器点对点，3 个绘制原语，TGS ⊃ kitty。ctest 67/67，像素级往返 100%（maxdiff=0），空闲 0 B/s。">
      <HomepageHeader />
      <main>
        <StatsStrip />
        <HomepageFeatures />
        <EvidenceShots />
      </main>
    </Layout>
  );
}
