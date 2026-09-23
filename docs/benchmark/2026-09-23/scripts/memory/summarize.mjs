import fs from 'node:fs';
import path from 'node:path';
const dir = path.dirname(new URL(import.meta.url).pathname);
const M = (b) => (b == null ? '-' : (b / 1048576).toFixed(1));
const files = process.argv.slice(2).length ? process.argv.slice(2)
  : fs.readdirSync(dir).filter((f) => f.startsWith('out_') && f.endsWith('.json')).sort();
for (const f of files) {
  const d = JSON.parse(fs.readFileSync(path.join(dir, f), 'utf8'));
  console.log(`\n## ${f}  mode=${d.mode} n=${d.n} ${d.ms_per_iter}ms/iter env=${JSON.stringify(d.env)}`);
  console.log('  i    rss   RssAnon  heapUsed  heapTotal  external  arrBuf   wasm   smapsPrivDirty');
  for (const s of d.series) {
    console.log(`  ${String(s.i).padStart(4)} ${M(s.rss).padStart(7)} ${M(s.RssAnon).padStart(8)} ${M(s.heapUsed).padStart(8)} ${M(s.heapTotal).padStart(9)} ${M(s.external).padStart(9)} ${M(s.arrayBuffers).padStart(7)} ${M(s.wasm_total).padStart(6)} ${M(s.smaps?.Private_Dirty).padStart(10)}  ${s.phase}`);
  }
  const a = d.series.filter((s) => s.phase === 'loop');
  if (a.length >= 3) {
    const mid = a[Math.floor(a.length / 2)], last = a[a.length - 1];
    const slope = (last.rss - mid.rss) / (last.i - mid.i);
    const slopeAll = (last.rss - a[0].rss) / (last.i - a[0].i);
    console.log(`  => rss slope 前半込み ${(slopeAll/1048576).toFixed(3)} MB/枚 / 後半のみ ${(slope/1048576).toFixed(3)} MB/枚`);
  }
}
