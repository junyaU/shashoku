// Satori + resvg で HTML ファイルを PNG にする。
// 使い方: node render.mjs <input.html> <width> <height> <output.png>
import fs from 'node:fs';
import path from 'node:path';
import satori from 'satori';
import { html as toVNode } from 'satori-html';
import { Resvg } from '@resvg/resvg-js';

const FONT_DIR = process.env.SATORI_FONT_DIR ||
  path.resolve('/tmp/claude-1000/-home-junya-src-github-com-junyaU-shashoku/94c9fae6-a9a0-40c9-ac43-94fa14abccb2/scratchpad/exp/bin/fonts');

export function loadFonts() {
  return [
    { name: 'Noto Sans JP', data: fs.readFileSync(path.join(FONT_DIR, 'NotoSansJP-Regular.otf')), weight: 400, style: 'normal' },
    { name: 'Noto Sans JP', data: fs.readFileSync(path.join(FONT_DIR, 'NotoSansJP-Bold.otf')), weight: 700, style: 'normal' },
    { name: 'Noto Sans', data: fs.readFileSync(path.join(FONT_DIR, 'NotoSans-Regular.ttf')), weight: 400, style: 'normal' },
  ];
}

export async function renderSvg(source, width, height, fonts) {
  const vnode = toVNode(source);
  return await satori(vnode, { width, height, fonts });
}

export function svgToPng(svg, width) {
  const resvg = new Resvg(svg, { fitTo: { mode: 'width', value: width } });
  return resvg.render().asPng();
}

async function main() {
  const [input, widthArg, heightArg, output] = process.argv.slice(2);
  if (!input || !widthArg || !heightArg || !output) {
    console.error('usage: node render.mjs <input.html> <width> <height> <output.png>');
    process.exit(2);
  }
  const width = Number(widthArg);
  const height = Number(heightArg);
  const source = fs.readFileSync(input, 'utf8');
  const fonts = loadFonts();
  const svg = await renderSvg(source, width, height, fonts);
  fs.writeFileSync(output.replace(/\.png$/, '.svg'), svg);
  fs.writeFileSync(output, svgToPng(svg, width));
  console.error(`ok: ${output} (${width}x${height})`);
}

if (import.meta.url === `file://${process.argv[1]}`) {
  main().catch((e) => {
    console.error(String(e && e.stack ? e.stack : e));
    process.exit(1);
  });
}
