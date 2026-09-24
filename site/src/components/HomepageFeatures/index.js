import clsx from 'clsx';
import Heading from '@theme/Heading';
import styles from './styles.module.css';

const FeatureList = [
  {
    token: 'TEXT · BOX · GRAPHIC',
    title: '机制 / 策略切割',
    description: (
      <>
        线上只有 3 个绘制原语与交互形状事件（KEY / CLICK / HOVER /
        POINTER）。按钮、列表、焦点、文本缓冲全部在程序侧组合——协议永远不绑死某个
        toolkit。
      </>
    ),
  },
  {
    token: 'G1 ⊃ G',
    title: 'kitty 超集',
    description: (
      <>
        帧共享 kitty 图形协议的 APC 通道：TGS 帧走 <code>G1;</code>{' '}
        子命名空间，程序也可以直接发 kitty 原生帧——解码后像素直接落画布，
        z 序钉在字符底之上、场景元素之下。
      </>
    ),
  },
  {
    token: 'maxdiff=0',
    title: '像素级验证',
    description: (
      <>
        每一项声明都有字节级证据：pty 捕获 + PIL 权威回放 + ground-truth
        逐像素对比。z 序遮挡 <code>120000/120000 = 100%</code>，
        单测 ctest 67/67。
      </>
    ),
  },
  {
    token: '0 B/s',
    title: '诚实的性能',
    description: (
      <>
        脏区传输空闲 0 B/s，120Hz 锚点节拍 123.7fps，240 门控 245.3fps
        （上限 250）；全屏翻页 109.9fps 如实标注为诚实上限——每个数字可复现、
        都带边界说明。
      </>
    ),
  },
];

function Feature({token, title, description}) {
  return (
    <div className="col col--6">
      <div className={styles.card}>
        <span className={styles.token}>{token}</span>
        <Heading as="h3">{title}</Heading>
        <p>{description}</p>
      </div>
    </div>
  );
}

export default function HomepageFeatures() {
  return (
    <section className={styles.features}>
      <div className="container">
        <div className="row">
          {FeatureList.map((props, idx) => (
            <Feature key={idx} {...props} />
          ))}
        </div>
      </div>
    </section>
  );
}
