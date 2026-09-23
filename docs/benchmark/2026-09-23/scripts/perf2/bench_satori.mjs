// Satori + resvg の常駐・同時生成コストの計測。
// フォントの Buffer は起動時に 1 回だけ読み、1 枚ごとに setImmediate で譲る
// （EXP/satori/memory/REPORT.md の結論に従った「正しい使い方」）。
//
// 使い方:
//   node bench_satori.mjs resident <out.json>
//   node bench_satori.mjs conc-promise <N> <rounds> <out.json>
//   node bench_satori.mjs conc-worker  <N> <rounds> <out.json>
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { Worker, isMainThread, parentPort, workerData } from 'node:worker_threads';

const EXP = '/tmp/claude-1000/-home-junya-src-github-com-junyaU-shashoku/' +
  '94c9fae6-a9a0-40c9-ac43-94fa14abccb2/scratchpad/exp';
const NM = path.join(EXP, 'satori', 'node_modules');
const SELF = pathToFileURL(process.argv[1] ?? '').href;

const satori = (await import(pathToFileURL(path.join(NM, 'satori/dist/index.js')).href)).default;
const { html: toVNode } = await import(pathToFileURL(path.join(NM, 'satori-html/dist/index.js')).href);
const resvgMod = await import(pathToFileURL(path.join(NM, '@resvg/resvg-js/index.js')).href);
const Resvg = resvgMod.Resvg ?? resvgMod.default.Resvg;

const FONT_DIR = path.join(EXP, 'bin', 'fonts');

function loadFonts() {
  return [
    { name: 'Noto Sans JP', data: fs.readFileSync(path.join(FONT_DIR, 'NotoSansJP-Regular.otf')), weight: 400, style: 'normal' },
    { name: 'Noto Sans JP', data: fs.readFileSync(path.join(FONT_DIR, 'NotoSansJP-Bold.otf')), weight: 700, style: 'normal' },
    { name: 'Noto Sans', data: fs.readFileSync(path.join(FONT_DIR, 'NotoSans-Regular.ttf')), weight: 400, style: 'normal' },
  ];
}

// EXP/satori/args.txt と同じ
const CASES = fs.readFileSync(path.join(EXP, 'satori', 'args.txt'), 'utf8')
  .trim().split('\n').map((l) => {
    const [id, w, h] = l.trim().split(/\s+/);
    return { name: `case${id}`, width: Number(w), height: Number(h) };
  });

const SOURCES = CASES.map((c) => fs.readFileSync(path.join(EXP, 'satori', `${c.name}.html`), 'utf8'));

const yieldOnce = () => new Promise((r) => setImmediate(r));

function pngSize(buf) {
  return { width: buf.readUInt32BE(16), height: buf.readUInt32BE(20) };
}

function procStatus(key) {
  const t = fs.readFileSync('/proc/self/status', 'utf8');
  const m = t.match(new RegExp('^' + key + ':\\s+(\\d+) kB', 'm'));
  return m ? Number(m[1]) * 1024 : -1;
}

const ns = () => process.hrtime.bigint();
const ms = (a, b) => Number(b - a) / 1e6;

// 1 枚。譲りは呼び出し側で行う（時間の内訳を分けるため）
async function renderOne(i, fonts) {
  const c = CASES[i];
  const t0 = ns();
  const svg = await satori(toVNode(SOURCES[i]), { width: c.width, height: c.height, fonts });
  const t1 = ns();
  const png = new Resvg(svg, { fitTo: { mode: 'width', value: c.width } }).render().asPng();
  const t2 = ns();
  return { satori_ms: ms(t0, t1), resvg_ms: ms(t1, t2), total_ms: ms(t0, t2), png };
}

function median(v) {
  if (!v.length) return 0;
  const s = [...v].sort((a, b) => a - b);
  const n = s.length;
  return n % 2 ? s[(n - 1) / 2] : (s[n / 2 - 1] + s[n / 2]) / 2;
}

async function runResident(out) {
  const rssStart = procStatus('VmRSS');
  const fonts = loadFonts();
  const rssAfterFonts = procStatus('VmRSS');
  const rows = [];
  for (let i = 0; i < CASES.length; i++) {
    for (let w = 0; w < 3; w++) { await renderOne(i, fonts); await yieldOnce(); }
    const sat = [], res = [], tot = [], wy = [];
    let last = null;
    for (let k = 0; k < 20; k++) {
      const t0 = ns();
      const r = await renderOne(i, fonts);
      await yieldOnce();
      const t3 = ns();
      sat.push(r.satori_ms); res.push(r.resvg_ms); tot.push(r.total_ms); wy.push(ms(t0, t3));
      last = r;
    }
    const size = pngSize(last.png);
    rows.push({
      case: CASES[i].name, width: size.width, height: size.height, png_bytes: last.png.length,
      satori_ms: median(sat), resvg_ms: median(res),
      total_ms_excl_yield: median(tot), total_ms_incl_yield: median(wy),
      min_ms_excl_yield: Math.min(...tot), max_ms_excl_yield: Math.max(...tot),
      rss_after_bytes: procStatus('VmRSS'),
    });
  }
  const obj = {
    mode: 'resident', warmup: 3, iters: 20, node: process.version, cases: rows,
    rss_start_bytes: rssStart, rss_after_fonts_bytes: rssAfterFonts,
    rss_end_bytes: procStatus('VmRSS'), memoryUsage_rss_end: process.memoryUsage().rss,
    vmhwm_bytes: procStatus('VmHWM'),
  };
  fs.writeFileSync(out, JSON.stringify(obj, null, 2) + '\n');
  console.log(JSON.stringify(obj, null, 2));
}

// (a) 1 プロセス・N 本の並行チェーン（JS は単一スレッド）
async function runConcPromise(n, rounds, out) {
  const fonts = loadFonts();
  for (let i = 0; i < CASES.length; i++) { await renderOne(i, fonts); await yieldOnce(); }  // ウォームアップ
  const rssAfterWarm = procStatus('VmRSS');
  let pages = 0;
  const t0 = ns();
  await Promise.all(Array.from({ length: n }, async () => {
    for (let r = 0; r < rounds; r++) {
      for (let i = 0; i < CASES.length; i++) { await renderOne(i, fonts); await yieldOnce(); pages++; }
    }
  }));
  const t1 = ns();
  const wall = ms(t0, t1) / 1000;
  const obj = {
    mode: 'conc-promise', threads: n, rounds, pages, wall_s: wall, pages_per_sec: pages / wall,
    rss_after_warm_bytes: rssAfterWarm, rss_end_bytes: procStatus('VmRSS'),
    vmhwm_bytes: procStatus('VmHWM'), node: process.version,
  };
  fs.writeFileSync(out, JSON.stringify(obj, null, 2) + '\n');
  console.log(JSON.stringify(obj, null, 2));
}

// (b) worker_threads。各ワーカーが自分でフォントを読み、独立に描く
async function runConcWorker(n, rounds, out) {
  const workers = [];
  const ready = [];
  const done = [];
  for (let i = 0; i < n; i++) {
    const w = new Worker(new URL(SELF), { workerData: { rounds } });
    workers.push(w);
    ready.push(new Promise((res) => w.once('message', (m) => { if (m === 'ready') res(); })));
    done.push(new Promise((res, rej) => {
      w.on('message', (m) => { if (m && m.done) res(m); });
      w.on('error', rej);
    }));
  }
  await Promise.all(ready);
  const rssAfterWarm = procStatus('VmRSS');
  const t0 = ns();
  for (const w of workers) w.postMessage('go');
  const results = await Promise.all(done);
  const t1 = ns();
  const wall = ms(t0, t1) / 1000;
  const pages = results.reduce((a, r) => a + r.pages, 0);
  const obj = {
    mode: 'conc-worker', threads: n, rounds, pages, wall_s: wall, pages_per_sec: pages / wall,
    rss_after_warm_bytes: rssAfterWarm, rss_end_bytes: procStatus('VmRSS'),
    vmhwm_bytes: procStatus('VmHWM'), node: process.version,
  };
  for (const w of workers) await w.terminate();
  fs.writeFileSync(out, JSON.stringify(obj, null, 2) + '\n');
  console.log(JSON.stringify(obj, null, 2));
}

async function workerMain() {
  const rounds = workerData.rounds;
  const fonts = loadFonts();
  for (let i = 0; i < CASES.length; i++) { await renderOne(i, fonts); await yieldOnce(); }
  parentPort.postMessage('ready');
  await new Promise((res) => parentPort.once('message', (m) => { if (m === 'go') res(); }));
  let pages = 0;
  for (let r = 0; r < rounds; r++) {
    for (let i = 0; i < CASES.length; i++) { await renderOne(i, fonts); await yieldOnce(); pages++; }
  }
  parentPort.postMessage({ done: true, pages });
}

if (!isMainThread) {
  await workerMain();
} else {
  const [mode, ...rest] = process.argv.slice(2);
  if (mode === 'resident' && rest.length === 1) await runResident(rest[0]);
  else if (mode === 'conc-promise' && rest.length === 3) await runConcPromise(Number(rest[0]), Number(rest[1]), rest[2]);
  else if (mode === 'conc-worker' && rest.length === 3) await runConcWorker(Number(rest[0]), Number(rest[1]), rest[2]);
  else { console.error('bad arguments'); process.exit(2); }
}
