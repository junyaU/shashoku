// 「普段どおりの HTML」をそのまま Satori に通す実験用。
// --stage=1  前処理なし（入力そのまま）
// --stage=2  <style> から擬似要素（::before/::after）の規則だけを機械的に落とす
// --stage=3  stage2 に加えて、Satori が必ず拒む 2 点を機械的に直す:
//              (a) 子が 2 つ以上あるのに display が無い要素に display:flex を入れる
//              (b) display:grid を display:flex に読み替える
//            いずれも「デザインのやり直し」ではなく、規則に沿った一律の書き換え。
import fs from 'node:fs';
import path from 'node:path';
import { html as toVNode } from 'satori-html';
import satori from 'satori';
import { Resvg } from '@resvg/resvg-js';
import { loadFonts } from '../render.mjs';

const argv = process.argv.slice(2);
const stage = Number((argv.find((a) => a.startsWith('--stage=')) || '--stage=1').split('=')[1]);
const [input, widthArg, heightArg, output] = argv.filter((a) => !a.startsWith('--'));
const width = Number(widthArg), height = Number(heightArg);

let source = fs.readFileSync(input, 'utf8');

// stage2: <style> の中から、セレクタに :: を含む規則だけを落とす（宣言の中身は触らない）
let droppedRules = 0;
if (stage >= 2) {
  source = source.replace(/<style[^>]*>([\s\S]*?)<\/style>/gi, (m, css) => {
    const kept = css.replace(/([^{}]+)\{([^{}]*)\}/g, (rule, sel) => {
      if (sel.includes('::')) { droppedRules++; return ''; }
      return rule;
    });
    return m.replace(css, kept);
  });
}

const vnode = toVNode(source);

// stage3: VNode を歩いて、Satori が必ず弾く点を一律の規則で書き換える（ケースごとの手当ては一切しない）
//   (a) display: grid / inline-block / inline-flex -> flex
//   (b) 子が文字列でない（＝要素の子を持つ）のに display が無い要素 -> display:flex + flex-direction:column
//       （ブラウザの block 整形文脈＝子を縦に積む、の代用。Satori は要素の子を持つ div に block を許さない）
//   (c) z-index を削除（Satori は「not supported」で例外を投げる）
//   (d) 相対パスの <img src> を、入力 HTML の隣のファイルから読んで data URL に置換
const stats = { grid: 0, inlineBlock: 0, addedFlex: 0, zIndex: 0, img: 0 };
const srcDir = path.dirname(path.resolve(input));
function walk(node) {
  if (!node || typeof node !== 'object') return;
  const props = node.props || {};
  const kids = props.children;
  const arr = (Array.isArray(kids) ? kids : (kids === undefined || kids === null ? [] : [kids])).flat(9);
  if (typeof node.type === 'string') {
    const st = props.style || {};
    if (st.display === 'grid') { st.display = 'flex'; stats.grid++; }
    if (st.display === 'inline-block' || st.display === 'inline-flex') { st.display = 'flex'; stats.inlineBlock++; }
    if ('zIndex' in st) { delete st.zIndex; stats.zIndex++; }
    // Satori の実際の判定は「children が文字列でない <div>」（メッセージの「子が 2 つ以上」は誤り）
    if (kids != null && typeof kids !== 'string' && !st.display) {
      st.display = 'flex';
      if (!st.flexDirection) st.flexDirection = 'column';
      stats.addedFlex++;
    }
    props.style = st;
    if (node.type === 'img' && typeof props.src === 'string' && !/^(https?:|data:)/.test(props.src)) {
      const f = path.resolve(srcDir, props.src);
      if (fs.existsSync(f)) {
        props.src = 'data:image/png;base64,' + fs.readFileSync(f).toString('base64');
        stats.img++;
      }
    }
    node.props = props;
  }
  for (const k of arr) if (k && typeof k === 'object') walk(k);
}
if (stage >= 3) walk(vnode);

const fonts = loadFonts();
const svg = await satori(vnode, { width, height, fonts });
fs.writeFileSync(output.replace(/\.png$/, '.svg'), svg);
fs.writeFileSync(output, new Resvg(svg, { fitTo: { mode: 'width', value: width } }).render().asPng());
console.error(`ok: ${output} (${width}x${height}) stage=${stage} droppedCssRules=${droppedRules} ${JSON.stringify(stage >= 3 ? stats : {})}`);
