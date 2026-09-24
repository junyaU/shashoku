import fs from 'node:fs';
import v8 from 'node:v8';
import { wasmSnapshot, wasmTotalBytes } from './wasmhook.mjs';

const kb = (s, key) => {
  const m = s.match(new RegExp('^' + key + ':\\s+(\\d+) kB', 'm'));
  return m ? Number(m[1]) * 1024 : null;
};

export function procStatus() {
  const t = fs.readFileSync('/proc/self/status', 'utf8');
  return {
    VmRSS: kb(t, 'VmRSS'), RssAnon: kb(t, 'RssAnon'), RssFile: kb(t, 'RssFile'),
    RssShmem: kb(t, 'RssShmem'), VmHWM: kb(t, 'VmHWM'), VmSize: kb(t, 'VmSize'),
  };
}

export function smapsRollup() {
  try {
    const t = fs.readFileSync('/proc/self/smaps_rollup', 'utf8');
    return { Rss: kb(t, 'Rss'), Pss: kb(t, 'Pss'), Private_Clean: kb(t, 'Private_Clean'),
             Private_Dirty: kb(t, 'Private_Dirty'), Anonymous: kb(t, 'Anonymous') };
  } catch { return null; }
}

export function gc2() { global.gc(); global.gc(); }

export function snap(i) {
  const mu = process.memoryUsage();
  const hs = v8.getHeapStatistics();
  const ps = procStatus();
  return {
    i,
    rss: mu.rss, heapTotal: mu.heapTotal, heapUsed: mu.heapUsed,
    external: mu.external, arrayBuffers: mu.arrayBuffers,
    v8_total_heap_size: hs.total_heap_size, v8_used_heap_size: hs.used_heap_size,
    v8_malloced_memory: hs.malloced_memory, v8_external_memory: hs.external_memory,
    v8_heap_size_limit: hs.heap_size_limit,
    VmRSS: ps.VmRSS, RssAnon: ps.RssAnon, RssFile: ps.RssFile, VmHWM: ps.VmHWM,
    smaps: smapsRollup(),
    wasm_total: wasmTotalBytes(), wasm: wasmSnapshot(),
  };
}

export function mb(b) { return b == null ? null : +(b / 1048576).toFixed(1); }

export function writeJson(name, obj) {
  const p = new URL(name, import.meta.url);
  fs.writeFileSync(p, JSON.stringify(obj, null, 2));
  return p.pathname;
}
