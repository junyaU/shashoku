// WebAssembly.Memory の実体を捕まえる。satori / render.mjs より前に import すること。
// （ESM の import は宣言順に評価されるので、先頭に書けば先に効く）
const records = [];

function label_of(exports) {
  const names = Object.keys(exports || {});
  if (names.some((n) => n.startsWith('hb_'))) return 'harfbuzz';
  if (names.some((n) => n.startsWith('_embind') || n.includes('embind'))) return 'yoga';
  return 'unknown';
}

function collect(instance, source) {
  try {
    const ex = instance && instance.exports;
    if (!ex) return;
    for (const [k, v] of Object.entries(ex)) {
      if (v instanceof OrigMemory) {
        records.push({ how: source + ':export:' + k, label: label_of(ex), mem: v,
                       exportCount: Object.keys(ex).length });
      }
    }
  } catch { /* ignore */ }
}

const OrigMemory = WebAssembly.Memory;
const MemoryProxy = new Proxy(OrigMemory, {
  construct(t, a) {
    const m = Reflect.construct(t, a);
    records.push({ how: 'ctor', label: 'ctor', mem: m, desc: JSON.stringify(a[0] || {}) });
    return m;
  },
});
WebAssembly.Memory = MemoryProxy;

const OrigInstance = WebAssembly.Instance;
WebAssembly.Instance = new Proxy(OrigInstance, {
  construct(t, a) {
    const i = Reflect.construct(t, a);
    collect(i, 'new Instance');
    return i;
  },
});

const origInstantiate = WebAssembly.instantiate;
WebAssembly.instantiate = function (...args) {
  const p = origInstantiate.apply(this, args);
  return p.then((r) => { collect(r && r.instance ? r.instance : r, 'instantiate'); return r; });
};
if (WebAssembly.instantiateStreaming) {
  const origStreaming = WebAssembly.instantiateStreaming;
  WebAssembly.instantiateStreaming = function (...args) {
    const p = origStreaming.apply(this, args);
    return p.then((r) => { collect(r && r.instance ? r.instance : r, 'instantiateStreaming'); return r; });
  };
}

export function wasmSnapshot() {
  // 同じ Memory を複数回拾うことがあるので実体で重複を除く
  const seen = new Set();
  const out = [];
  for (const r of records) {
    if (seen.has(r.mem)) continue;
    seen.add(r.mem);
    let bytes = null;
    try { bytes = r.mem.buffer.byteLength; } catch { bytes = -1; }
    out.push({ how: r.how, label: r.label, bytes });
  }
  return out;
}
export function wasmTotalBytes() {
  return wasmSnapshot().reduce((a, b) => a + (b.bytes > 0 ? b.bytes : 0), 0);
}
