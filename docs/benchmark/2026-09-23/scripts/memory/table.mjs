import fs from 'node:fs'; import path from 'node:path';
const dir = path.dirname(new URL(import.meta.url).pathname);
const M = (b) => (b == null ? '-' : +(b / 1048576).toFixed(1));
const rows = [];
for (const f of fs.readdirSync(dir).filter((x) => x.startsWith('out_') && x.endsWith('.json')).sort()) {
  const d = JSON.parse(fs.readFileSync(path.join(dir, f), 'utf8'));
  const a = d.series.filter((s) => s.phase === 'loop');
  if (!a.length) continue;
  const first = d.series[0], last = a[a.length - 1];
  const mid = a[Math.floor(a.length / 2)];
  const slope2 = (last.rss - mid.rss) / (last.i - mid.i) / 1048576;
  const env = Object.entries(d.env).filter(([, v]) => v != null).map(([k, v]) => `${k}=${v}`).join(' ');
  const mi = Object.keys(process.env).filter((k) => k.startsWith('MIMALLOC')).join(' ');
  rows.push({ file: f.replace(/^out_|\.json$/g, ''), mode: d.mode, case: d.case, n: d.n,
    ms: d.ms_per_iter, rss0: M(first.rss), rssN: M(last.rss),
    slope: +slope2.toFixed(3), heap0: M(first.heapUsed), heapN: M(last.heapUsed),
    ext0: M(first.external), extN: M(last.external), wasm0: M(first.wasm_total), wasmN: M(last.wasm_total),
    failed: d.failed ? `i=${d.failed.i}` : '', env });
}
console.log('| run | mode | n | ms/枚 | RSS 開始 | RSS 終了 | RSS 傾き(後半) MB/枚 | heapUsed | external | wasm | 備考 |');
console.log('|---|---|---|---|---|---|---|---|---|---|---|');
for (const r of rows) console.log(`| ${r.file} | ${r.mode} | ${r.n} | ${r.ms} | ${r.rss0} | ${r.rssN} | **${r.slope}** | ${r.heap0}→${r.heapN} | ${r.ext0}→${r.extN} | ${r.wasm0}→${r.wasmN} | ${r.env}${r.failed ? ' 失敗 ' + r.failed : ''} |`);
