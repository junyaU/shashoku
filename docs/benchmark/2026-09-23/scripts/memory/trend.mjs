// 使い方: node --expose-gc trend.mjs <mode> <case> <n> <sample> [outfile]
//   mode: full | satori | resvg | resvg_nosysfont | satori_freshbuf | noop
// 各サンプル点では gc() を 2 回呼んでから測る。
import { snap, gc2, writeJson } from './lib.mjs';     // wasmhook はこの中で先に読まれる
import fs from 'node:fs';
import path from 'node:path';
import { loadFonts, renderSvg } from '../render.mjs';
import { Resvg, renderAsync } from '@resvg/resvg-js';

const [mode = 'full', caseId = '07', nArg = '200', sampleArg = '10', outArg] = process.argv.slice(2);
const N = Number(nArg), SAMPLE = Number(sampleArg);
const dir = path.dirname(new URL('../render.mjs', import.meta.url).pathname);

const args = fs.readFileSync(path.join(dir, 'args.txt'), 'utf8').trim().split('\n')
  .map((l) => { const [n, w, h] = l.trim().split(/\s+/); return { n, w: +w, h: +h }; });
const c = args.find((a) => a.n === caseId);
const src = fs.readFileSync(path.join(dir, `case${caseId}.html`), 'utf8');

const fontFiles = ['NotoSansJP-Regular.otf', 'NotoSansJP-Bold.otf', 'NotoSans-Regular.ttf'];
const fontDir = process.env.SATORI_FONT_DIR || path.resolve(dir, '../bin/fonts');
const rawFonts = fontFiles.map((f) => fs.readFileSync(path.join(fontDir, f)));
const meta = [{ name: 'Noto Sans JP', weight: 400 }, { name: 'Noto Sans JP', weight: 700 },
               { name: 'Noto Sans', weight: 400 }];
const freshFonts = () => rawFonts.map((d, i) =>
  ({ name: meta[i].name, data: Buffer.from(d), weight: meta[i].weight, style: 'normal' }));

const fonts = loadFonts();                       // 使い回す Buffer（既定の使い方）
const resvgOpts = mode === 'resvg_nosysfont' || process.env.NO_SYS_FONT === '1'
  ? { fitTo: { mode: 'width', value: c.w }, font: { loadSystemFonts: false } }
  : { fitTo: { mode: 'width', value: c.w } };

// resvg 単独モード用の固定 SVG（satori を 1 回だけ回して作る）
const minimalSvg = `<svg xmlns="http://www.w3.org/2000/svg" width="${c.w}" height="${c.h}"><rect width="100%" height="100%" fill="#eee"/><circle cx="${c.w / 2}" cy="${c.h / 2}" r="${Math.min(c.w, c.h) / 3}" fill="#37c"/></svg>`;
let fixedSvg = null;
if (mode.startsWith('resvg') && !mode.startsWith('resvg_minimal')) fixedSvg = await renderSvg(src, c.w, c.h, fonts);

let sink = 0;
let iterNo = 0;
const yieldK = Number(process.env.YIELD_EVERY || '10');
async function once() {
  if (mode === 'noop') { sink += Math.random(); return; }
  if (mode === 'satori') { const s = await renderSvg(src, c.w, c.h, fonts); sink += s.length; return; }
  if (mode === 'satori_freshbuf') { const s = await renderSvg(src, c.w, c.h, freshFonts()); sink += s.length; return; }
  if (mode === 'resvg' || mode === 'resvg_nosysfont') {
    const png = new Resvg(fixedSvg, resvgOpts).render().asPng(); sink += png.length; return;
  }
  if (mode === 'resvg_ctor') { const r = new Resvg(fixedSvg, resvgOpts); sink += r.width; return; }
  if (mode === 'resvg_render') { const img = new Resvg(fixedSvg, resvgOpts).render(); sink += img.width; return; }
  if (mode === 'resvg_async') { const img = await renderAsync(fixedSvg, resvgOpts); sink += img.asPng().length; return; }
  if (mode === 'resvg_yield') {
    const png = new Resvg(fixedSvg, resvgOpts).render().asPng(); sink += png.length;
    await new Promise((r) => setImmediate(r)); return;                 // イベントループを 1 回まわす
  }
  if (mode === 'resvg_gc_each') {                       // 譲らずに毎回 gc()（GC/ファイナライザか mimalloc かの切り分け）
    const png = new Resvg(fixedSvg, resvgOpts).render().asPng(); sink += png.length;
    global.gc(); return;
  }
  if (mode === 'resvg_yield_k') {                      // K 回に 1 回だけイベントループに譲る
    const png = new Resvg(fixedSvg, resvgOpts).render().asPng(); sink += png.length;
    if (yieldK > 0 && (iterNo % yieldK === 0)) await new Promise((r) => setImmediate(r));
    return;
  }
  if (mode === 'full_yield_k') {
    const svg3 = await renderSvg(src, c.w, c.h, fonts);
    const png = new Resvg(svg3, resvgOpts).render().asPng(); sink += png.length;
    if (yieldK > 0 && (iterNo % yieldK === 0)) await new Promise((r) => setImmediate(r));
    return;
  }
  if (mode === 'resvg_minimal') {
    const png = new Resvg(minimalSvg, resvgOpts).render().asPng(); sink += png.length; return;
  }
  if (mode === 'resvg_minimal_yield') {
    const png = new Resvg(minimalSvg, resvgOpts).render().asPng(); sink += png.length;
    await new Promise((r) => setImmediate(r)); return;
  }
  if (mode === 'full_yield') {
    const svg2 = await renderSvg(src, c.w, c.h, fonts);
    const png = new Resvg(svg2, resvgOpts).render().asPng(); sink += png.length;
    await new Promise((r) => setImmediate(r)); return;
  }
  const svg = await renderSvg(src, c.w, c.h, fonts);
  const png = new Resvg(svg, resvgOpts).render().asPng(); sink += png.length;
}

const series = [];
gc2(); series.push({ ...snap(0), phase: 'before' });
const t0 = Date.now();
let failed = null;
for (let i = 1; i <= N; i++) {
  iterNo = i;
  try { await once(); } catch (e) { failed = { i, error: String(e && e.message || e) }; break; }
  if (i % SAMPLE === 0 || i === N) { gc2(); series.push({ ...snap(i), phase: 'loop' }); }
}
const elapsed = Date.now() - t0;
gc2(); await new Promise((r) => setTimeout(r, 300)); gc2();
series.push({ ...snap(N), phase: 'after_gc_settle' });

const out = {
  mode, case: caseId, width: c.w, height: c.h, n: N, sample: SAMPLE,
  elapsed_ms: elapsed, ms_per_iter: +(elapsed / N).toFixed(2),
  env: { YIELD_EVERY: process.env.YIELD_EVERY ?? null,
         MIMALLOC_PURGE_DELAY: process.env.MIMALLOC_PURGE_DELAY ?? null,
         MALLOC_ARENA_MAX: process.env.MALLOC_ARENA_MAX ?? null,
         MALLOC_TRIM_THRESHOLD_: process.env.MALLOC_TRIM_THRESHOLD_ ?? null,
         MALLOC_MMAP_THRESHOLD_: process.env.MALLOC_MMAP_THRESHOLD_ ?? null,
         UV_THREADPOOL_SIZE: process.env.UV_THREADPOOL_SIZE ?? null },
  node: process.version, sink, failed, series,
};
const name = outArg || `out_${mode}_case${caseId}_n${N}.json`;
console.log(writeJson(name, out));
if (failed) console.error(`!! FAILED at i=${failed.i}: ${failed.error}`);
const f = series[0], l = series[series.length - 1];
const M = (b) => (b / 1048576).toFixed(1);
console.error(`${mode} n=${N}: rss ${M(f.rss)} -> ${M(l.rss)} MB | heapUsed ${M(f.heapUsed)} -> ${M(l.heapUsed)} | ext ${M(f.external)} -> ${M(l.external)} | wasm ${M(f.wasm_total)} -> ${M(l.wasm_total)} | RssAnon ${M(f.RssAnon)} -> ${M(l.RssAnon)} | ${(elapsed / N).toFixed(1)} ms/iter`);
