> 2026-09-23 夜のセッションで行った Satori + resvg のメモリ調査の記録をそのまま収めたもの。`EXP/…` はそのセッションのスクラッチ（/tmp。すでに無い可能性がある）。結論: リークではなく解放の先送り（イベントループを回せば頭打ち）。

# Satori + resvg（Node）の RSS が戻らない件 — 切り分け

対象: `EXP=/tmp/claude-1000/-home-junya-src-github-com-junyaU-shashoku/94c9fae6-a9a0-40c9-ac43-94fa14abccb2/scratchpad/exp/satori`
Node v20.18.1 / satori 0.33.5 / satori-html 0.3.2 / @resvg/resvg-js 2.6.2 / harfbuzzjs 0.10.0 / Linux 5.15 (WSL2) / glibc。
測定スクリプトは `memory/trend.mjs`（+ `lib.mjs` / `wasmhook.mjs`）。各サンプル点で `global.gc()` を 2 回呼んでから
`process.memoryUsage()` / `v8.getHeapStatistics()` / `/proc/self/status` / `/proc/self/smaps_rollup` /
`WebAssembly.Memory#buffer.byteLength`（コンストラクタと `instantiate` をフックして実体を捕捉）を記録する。
測定はすべて別プロセス。既定のケースは case07（1080×1080）。

**結論を先に**: 増えているのはほぼ全部ネイティブ側（resvg の pixmap）で、リークではなく
**「イベントループが 1 回も回らない限りネイティブのメモリが解放されない」**という解放の先送り。
`global.gc()` では解放されず、`setImmediate` を 1 回はさむだけで完全に頭打ちになる。
別件として、フォントの Buffer を毎回作り直すと harfbuzz の wasm 線形メモリが 1 枚あたり約 9.3 MB 増え、これは恒久的に残る。

---

## 1. 全測定の一覧

「RSS 傾き(後半)」は後半のサンプル点から求めた 1 枚あたりの RSS 増分。0 なら頭打ち。
（注意: 各 JSON の最後の行 `after_gc_settle` は 300 ms の `setTimeout` 後の値で、ここでイベントループが回るため
大きく下がることがある。前回の走行ログの 1 行要約はこの値を使っていて増加を過小に見せていたので、この表では
ループ最後のサンプル点を使っている。）

| run | mode | n | ms/枚 | RSS 開始 | RSS 終了 | RSS 傾き(後半) MB/枚 | heapUsed | external | wasm | 備考 |
|---|---|---|---|---|---|---|---|---|---|---|
| base_full | full | 400 | 22.2 | 86.6 | 2105.2 | **4.731** | 6.3→34.7 | 22.1→59.3 | 0→26.8 |  |
| base_full_long | full | 1000 | 22.41 | 84.6 | 4942.6 | **4.728** | 6.3→34.2 | 22.1→59.3 | 0→26.8 |  |
| base_noop | noop | 400 | 0.24 | 84.8 | 90.3 | **0** | 6.3→8.5 | 22.1→23.6 | 0→0 |  |
| base_resvg | resvg | 400 | 15.65 | 180 | 2053.4 | **4.724** | 33.9→33.9 | 59.3→59.3 | 26.8→26.8 |  |
| base_resvg_nosysfont | resvg_nosysfont | 400 | 15.1 | 161.1 | 2064.3 | **4.763** | 34→33.9 | 59.3→59.3 | 26.8→26.8 |  |
| base_satori | satori | 400 | 6.21 | 87.5 | 199.6 | **0.008** | 6.3→34.7 | 22.1→59.3 | 0→26.8 |  |
| fix_full_long | full | 1000 | 23.98 | 84.5 | 4923.3 | **4.713** | 6.3→34.9 | 22.1→59.3 | 0→26.8 |  |
| fix_full_purge0 | full | 400 | 23.95 | 84.5 | 2094.5 | **4.713** | 6.3→34.7 | 22.1→59.3 | 0→26.9 |  |
| font_freshbuf_200 | satori_freshbuf | 200 | 63.88 | 85.1 | 2287 | **8.432** | 6.3→200.7 | 22.1→1959.3 | 0→1862.6 |  |
| font_freshbuf_small | satori_freshbuf | 60 | 72.82 | 85.2 | 1049.5 | **8.697** | 6.3→200 | 22.1→702.9 | 0→606.2 |  |
| iso_gc_each | resvg_gc_each | 400 | 21.08 | 180 | 2049.3 | **4.722** | 34→33.9 | 59.3→59.3 | 26.8→26.8 |  |
| iso_resvg_async | resvg_async | 200 | 16.31 | 161.4 | 305.8 | **-0.479** | 33.9→33.8 | 59.3→59.3 | 26.8→26.9 |  |
| iso_resvg_ctor | resvg_ctor | 400 | 2.94 | 161.7 | 221.8 | **0.095** | 34→33.9 | 59.3→59.3 | 26.9→26.9 |  |
| iso_resvg_render | resvg_render | 400 | 6.55 | 182.1 | 2051.7 | **4.844** | 34→33.9 | 59.3→59.3 | 26.8→26.8 |  |
| long_satori | satori | 2000 | 5.49 | 84.8 | 202.3 | **0** | 6.3→34.9 | 22.1→59.3 | 0→26.8 |  |
| malloc_full | full | 400 | 22.49 | 79.4 | 2086.4 | **4.72** | 6.3→34.7 | 22.1→59.3 | 0→26.8 | MALLOC_ARENA_MAX=1 MALLOC_TRIM_THRESHOLD_=0 MALLOC_MMAP_THRESHOLD_=131072 |
| malloc_resvg | resvg | 400 | 17.69 | 162.1 | 2051.3 | **4.724** | 34.1→33.9 | 59.3→59.3 | 26.9→26.9 | MALLOC_ARENA_MAX=1 MALLOC_TRIM_THRESHOLD_=0 MALLOC_MMAP_THRESHOLD_=131072 |
| malloc_satori | satori | 400 | 6.24 | 85.6 | 185.4 | **0.004** | 6.3→34.7 | 22.1→59.3 | 0→26.8 | MALLOC_ARENA_MAX=1 MALLOC_TRIM_THRESHOLD_=0 MALLOC_MMAP_THRESHOLD_=131072 |
| mi_decommit | resvg | 400 | 19.52 | 182.2 | 2040.6 | **4.707** | 34.2→33.8 | 59.3→59.3 | 26.8→26.8 |  |
| mi_noeager | resvg | 400 | 17.88 | 160.7 | 2043.1 | **4.707** | 34→33.8 | 59.3→59.3 | 26.8→26.8 |  |
| mi_purge0 | resvg | 400 | 17.51 | 161 | 2033.8 | **4.707** | 34→33.8 | 59.3→59.3 | 26.8→26.8 |  |
| mi_verbose | resvg | 60 | 15.08 | 159.1 | 466 | **NaN** | 34→33.8 | 59.3→59.3 | 26.8→26.8 |  |
| min_resvg | resvg_minimal | 400 | 13.17 | 84.9 | 1948.7 | **4.623** | 6.3→8.5 | 22.1→23.6 | 0→0 |  |
| min_resvg_yield | resvg_minimal_yield | 400 | 12.39 | 85 | 199.1 | **-0.001** | 6.3→8.8 | 22.1→39.6 | 0→16.4 |  |
| server_long | full_yield | 2000 | 20.41 | 83.8 | 218.6 | **-0.011** | 6.3→34.9 | 22.1→59.3 | 0→26.9 |  |
| server_long_purge0 | full_yield | 2000 | 24 | 84.6 | 199.2 | **0** | 6.3→34.4 | 22.1→59.3 | 0→26.9 | MIMALLOC_PURGE_DELAY=0 |
| size_resvg_c02 | resvg | 400 | 9.19 | 178 | 848.1 | **1.743** | 34.1→33.9 | 59.3→59.3 | 26.8→26.8 |  |
| size_resvg_c03 | resvg | 400 | 8.79 | 158.8 | 811.1 | **1.638** | 33.6→33.5 | 59.3→59.3 | 26.8→26.8 |  |
| yield_full | full_yield | 400 | 23.85 | 85 | 227.3 | **0.01** | 6.3→34.7 | 22.1→59.3 | 0→26.9 |  |
| yield_resvg | resvg_yield | 400 | 15.28 | 180.2 | 257.7 | **0.002** | 34→33.9 | 59.3→59.3 | 26.8→26.9 |  |
| yieldk_10 | resvg_yield_k | 400 | 15.06 | 162.1 | 268.1 | **-0.154** | 34→33.9 | 59.3→59.3 | 26.8→26.9 | YIELD_EVERY=10 |
| yieldk_50 | resvg_yield_k | 400 | 15.13 | 184.4 | 426.1 | **-0.026** | 33.8→33.9 | 59.3→59.3 | 26.8→26.9 | YIELD_EVERY=50 |

モード: `full`=satori→resvg、`satori`=satori だけ、`resvg`=固定 SVG を resvg だけ、`resvg_ctor`=`new Resvg()` だけ、
`resvg_render`=`render()` まで（`asPng()` しない）、`*_yield`=1 枚ごとに `await new Promise(setImmediate)`、
`*_yield_k`=K 枚に 1 回だけ譲る、`resvg_gc_each`=譲らずに毎回 `global.gc()`、`satori_freshbuf`=毎回 `Buffer.from()` で複製したフォントを渡す。

---

## 2. 仮説ごとの判定

### 仮説 1: V8 ヒープの成長（JS 側のリーク） → **否定**

- 反する証拠（実測）: `full` を 400 枚で RSS は +2018.6 MB 増えるのに、内訳は heapUsed +28.4 MB / heapTotal +53.5 MB /
  external +37.2 MB（うち wasm +26.8 MB）/ arrayBuffers +8.9 MB。RssAnon の増分 +2013.5 MB が RSS 増分とほぼ一致する。
- 反する証拠（実測）: `satori` だけを 2000 枚回しても heapUsed は 6.3 → 34.9 MB、external 22.1 → 59.3 MB で頭打ち
  （傾き 0.000 MB/枚）。1000 枚・2000 枚でも同じ水準に落ち着く。
- 支持する証拠（実測、ただし限定条件）: `satori_freshbuf`（毎回新しい Buffer）のときだけ heapUsed が 200 MB まで増える。
  これは仮説 4 の帰結で、既定の使い方では起きない。
- **結論: 既定の使い方では JS のリークはない。V8 ヒープは約 35 MB で頭打ち。**

### 仮説 2: wasm の線形メモリの成長 → **既定の使い方では否定。フォントの渡し方を誤ると恒久的に増える**

ソースを読んで分かったこと（`node_modules/satori/dist/index.js` 0.33.5 と、同版の src を GitHub から取った `memory/_satori_src/src/`）:

- wasm は 2 つ。`WebAssembly.Memory` を実体で数えると **harfbuzz 11,272,192 B（11.27 MB）と yoga 16,777,216 B（16.78 MB）**で、
  合計 26.9 MB。初期化後この 2 つだけで、描画ごとに新しい `Memory` は作られない。
- yoga: `src/satori.ts` の最後で `root.freeRecursive()` を呼んでいる（dist 側にも `o.freeRecursive()` がある）。
  実測でも yoga の 16.78 MB は 2000 枚まわして 1 バイトも変わらない。
  ただし `freeRecursive()` は `try/finally` に入っていないので、**描画の途中で例外が出た場合はノードが解放されない**
  （＝ソースから言える経路。今回は実測していない）。`pointScaleFactor` を指定したときに作る `Yoga.Config` も解放していない。
- harfbuzz: `src/harfbuzz.ts` の `hbFontCache = new WeakMap<opentype.Font, hbFont>` にキャッシュし、
  `FinalizationRegistry` で `opentype.Font` が回収されたときに `hbFont.destroy()` する。face と blob は作った直後に
  `destroy()` して参照を font に預けている。**寿命は「その opentype.Font が GC されるまで」**。
- 反する証拠（実測）: 同じ Buffer を使い回す既定の使い方では harfbuzz は 11.27 MB のまま 2000 枚まわしても変わらない。
- 支持する証拠（実測）: 毎回 `Buffer.from()` で複製して渡すと **harfbuzz の線形メモリが 1 枚あたり約 9.3 MB 増える**
  （60 枚で 618 MB、200 枚で 1862 MB）。これはフォント 3 本の合計バイト数 9.5 MB とほぼ一致する。
  `WebAssembly.Memory` は伸びても縮まないので、この分は**プロセスが終わるまで RSS に残る**。
- **結論: 既定の使い方では wasm は 26.9 MB で固定、原因ではない。ただしフォント Buffer を毎回作り直すと
  harfbuzz 側が 1 枚 9.3 MB ずつ恒久的に増える（仮説 4 と同じ現象の裏側）。**

### 仮説 3: ネイティブ側（resvg の Rust 領域 / アロケータの保持） → **支持。これが主因**

- 支持する証拠（実測、切り分け）:
  - `satori` だけ: **0.008 MB/枚**（頭打ち）
  - `resvg` だけ（固定 SVG）: **4.724 MB/枚**（直線）
  - `new Resvg()` だけで `render()` しない: **0.095 MB/枚**
  - `render()` まで（`asPng()` しない）: **4.844 MB/枚** → 増えているのは `render()` が返す pixmap
  - SVG の中身に依らない: satori 由来の複雑な SVG（`resvg`）でも、円 1 個の自作 SVG（`resvg_minimal`）でも
    1080×1080 なら 4.72 / 4.62 MB/枚でほぼ同じ
  - 画素数に比例: 1080×1080 → 4.72、1000×345 → 1.74、720×430 → 1.64 MB/枚。
    幅×高さ×4 バイト（4.45 / 1.32 / 1.18 MB）＋ 0.3〜0.4 MB にきれいに乗る
- アロケータ（保持か解放漏れか）:
  - glibc のつまみ（`MALLOC_ARENA_MAX=1` / `MALLOC_TRIM_THRESHOLD_=0` / `MALLOC_MMAP_THRESHOLD_=131072`）は
    **まったく効かない**（4.720 MB/枚のまま）。理由はソースで確認: `resvgjs.linux-x64-gnu.node` には
    **mimalloc が静的リンクされている**（バイナリ中に mimalloc の文字列。`MIMALLOC_VERBOSE=1` で
    `purge_delay: 10` / `arena_reserve: 1048576` などの mimalloc のログが実際に出る）。glibc の malloc は通らない。
  - mimalloc のつまみ（`MIMALLOC_PURGE_DELAY=0` / `MIMALLOC_PURGE_DECOMMITS=1` / `MIMALLOC_ARENA_EAGER_COMMIT=0`）も
    **増加そのものは止めない**（4.707 MB/枚）。効くのは「解放したあと OS に返すか」だけで、
    `MIMALLOC_PURGE_DELAY=0` だと最後の `after_gc_settle` で 2033.8 MB → 149.6 MB まで落ちる（既定だと 2049 → 1893 MB）。
- 決定的な実験（実測）:
  - **`global.gc()` を毎回呼んでも増える**（`resvg_gc_each`: 4.722 MB/枚）
  - **`setImmediate` を 1 回はさむだけで平坦**（`resvg_yield`: 0.002 MB/枚、`full_yield`: 0.010 MB/枚）
  - `renderAsync()` も平坦（-0.479 MB/枚）
  - K 枚に 1 回だけ譲ると、上限がほぼ K × 4.7 MB になる（K=10 → 約 300 MB で頭打ち、K=50 → 約 450 MB で頭打ち）
  - `full` モードは内部で `await`（satori は async）しているのに増える。**解決済み Promise の await（マイクロタスク）では足りず、
    マクロタスク 1 回（setImmediate / setTimeout / 実 I/O）が要る**
- 推測（ソースまでは追っていない）: `resvgjs.node` は napi-rs 製で `napi_wrap` を使っている。Node 18 以降、
  napi のファイナライザ（第 2 パス）は GC 中ではなくイベントループ上で実行されるため、ループが回らないと
  pixmap を持つ Rust オブジェクトが解放されない ── という説明が観測と整合する。Node 本体のソースでは確認していない。
- **結論: 増えているのはネイティブ（resvg の pixmap）。ただし「アロケータが抱え込んでいる」のではなく
  「イベントループが回るまで free 自体が呼ばれない」。アロケータの設定は、解放後に OS へ返すかだけを左右する。**

### 仮説 4: フォントのキャッシュ → **既定の使い方では問題なし。毎回別 Buffer を渡すと破綻する**

ソースを読んで分かったこと（キャッシュのキーと寿命）:

- `src/satori.ts`: `fontCache = WeakMap<fonts 配列, FontLoader>`（配列そのものの identity）。外れた場合は
  `fontDataIds = WeakMap<font.data, 連番 id>` から `id:name:weight:style:lang;` というキーを作り、
  `fontLoaderCache = Map<key, FontLoader>`（**LRU で最大 8 個**、dist 側では `up=8`）に当てる。
  → 配列を毎回インラインで作っても、**中の Buffer が同じなら同じ FontLoader に当たる**。
- `src/font.ts`: `cachedParsedFont = WeakMap<data, opentype.Font>` で opentype のパース結果を data の identity で保持。
  整形結果は FontLoader ごとの `shapedRunCache`（最大 256 件の LRU）。`addFonts` のたびにクリアされる。
- `src/harfbuzz.ts`: `hbFontCache = WeakMap<opentype.Font, hbFont>` ＋ `FinalizationRegistry` で `destroy()`。
- つまり**キーは全部「フォントデータの Buffer の identity」**で、寿命は GC 任せ（LRU 8 と WeakMap）。
- 実測（同じ Buffer を渡し続ける／毎回複製する の比較）:

| 渡し方 | n | RSS 終了 | RSS 傾き | heapUsed | wasm |
|---|---|---|---|---|---|
| 同じ Buffer（既定） | 2000 | 202.3 MB | 0.000 MB/枚 | 6.3→34.9 | 0→26.8 MB |
| 毎回 `Buffer.from()` | 200 | 2287.0 MB | **8.432 MB/枚** | 6.3→200.7 | 0→**1862.6 MB** |

- おまけ（実測）: `satori_freshbuf` は長く回すと**異常終了する**。2 回とも別の場所で落ちた。
  - `RuntimeError: memory access out of bounds` @ `hbjs.js` `Object.createFont`（satori の `Xc`→`og` 経由）
  - `RuntimeError: table index is out of bounds` @ `hbjs.js` `Object.destroy` ← `FinalizationRegistry.cleanupSome`
  原因の特定まではしていない。n=200 の走行は最後まで完走した（落ちる回数は一定ではない）。
- **結論: 同じ Buffer を使い回す限りキャッシュは正しく効いていて溜まらない。毎回複製して渡すと
  1 枚あたり約 9.3 MB（フォントのバイト数分）の wasm 線形メモリが恒久的に積み上がり、やがてクラッシュする。**

### 仮説 5: 頭打ちかリークか → **「譲らなければ線形（頭打ちなし）」「1 枚ごとに譲れば完全に頭打ち」**

| 条件 | n | RSS（ループ最後） | 1 枚あたり |
|---|---|---|---|
| `full`（譲らない） | 400 | 2105.2 MB | 4.731 MB/枚 |
| `full`（譲らない） | 1000 | 4942.6 MB | 4.728 MB/枚 |
| `full_yield`（毎回譲る） | 400 | 227.3 MB | 0.010 MB/枚 |
| `full_yield`（毎回譲る） | 2000 | 218.6 MB | **-0.011 MB/枚** |
| `full_yield` + `MIMALLOC_PURGE_DELAY=0` | 2000 | 199.2 MB | 0.000 MB/枚 |
| `resvg_yield_k` K=10 | 400 | 268.1 MB | 約 300 MB で頭打ち |
| `resvg_yield_k` K=50 | 400 | 426.1 MB | 約 450 MB で頭打ち |

- 譲らない場合、400 枚と 1000 枚で傾きが 4.731 / 4.728 とほぼ同じ。**飽和の兆候はまったくない。**
- 毎回譲る場合、100 枚目には 222 MB に達しそこから 2000 枚まで 218〜228 MB を行き来するだけ（むしろ微減）。
- **結論: リーク（無限に増える）ではなく「解放の先送り」。上限はイベントループを回す頻度で決まり、
  だいたい `基礎 200 MB + (譲るまでに描いた枚数) × 幅×高さ×4 バイト`。**

### 既知の観測との突き合わせ

`perf/rss.mjs`（case07 を 50 枚、`gc()` 後 447.7 MB）は上と整合する。あのループは `await renderSvg(...)` を
はさんでいるが、これは解決済み Promise のマイクロタスクなのでイベントループは回らない。
74.8 MB（フォント読込後）＋ satori のウォームアップ約 115 MB（`satori` 単独の頭打ち 200 MB − 起動時 85 MB）＋ 50 × 4.7 MB ≒ 425 MB（実測 447.7 MB）。
`gc()` を呼んでも戻らないのは、`gc()` では napi のファイナライザが走らないため。

---

## 3. 必ず答える問い

**(a) 増えているのは V8 ヒープか、wasm か、ネイティブか（割合）**
既定の使い方（`full`、400 枚、1080×1080）で RSS 増分 2018.6 MB の内訳は、
heapUsed +28.4 MB（1.4%）、wasm +26.8 MB（1.3%）、external のうち wasm 以外 +10.4 MB（0.5%）、
残り **約 1953 MB（96.8%）がネイティブ**（RssAnon の増分 2013.5 MB とほぼ一致）。
例外は「フォント Buffer を毎回作る」使い方で、このときだけ wasm が支配的になる（200 枚で 1862 MB）。

**(b) satori と resvg のどちらが主因**
**resvg**。satori だけなら 2000 枚でも 0.000 MB/枚で 202 MB に頭打ち。resvg だけで 4.72 MB/枚。
さらに絞ると `new Resvg()` ではなく **`render()` が返す pixmap**（幅×高さ×4 バイト）。

**(c) リークか保持（頭打ち）か**
どちらでもなく **「解放の先送り」**。イベントループが 1 回も回らない間は free が呼ばれないので線形に増え（頭打ちなし）、
1 回でも回せば全部戻って完全に頭打ちになる。恒久的に残るのは
(1) 初期化分（satori 約 130 MB / wasm 26.9 MB）と (2) フォント Buffer を作り直した場合の wasm 線形メモリだけ。

**(d) 回避策（試したもののみ）**

| 手 | 効果（実測） | 代償 |
|---|---|---|
| 1 枚ごとに `await new Promise(r => setImmediate(r))`（または `renderAsync`） | **これが本命**。2000 枚で 218 MB、傾き -0.011 MB/枚 | 実質なし（20.4 ms/枚） |
| ＋ `MIMALLOC_PURGE_DELAY=0` | 2000 枚で 199 MB（さらに約 20 MB 減） | 24.0 ms/枚（約 18% 遅い） |
| フォントの Buffer をプロセス内で 1 回だけ読んで使い回す | 2000 枚で wasm も heapUsed も平坦 | なし（`Buffer.from` すると 9.3 MB/枚＋クラッシュ） |
| K 枚に 1 回だけ譲る | 上限が K×4.7 MB 程度に固定される | K が大きいほど上限も大きい |
| `global.gc()` を毎回 | **効かない**（4.722 MB/枚） | 21.1 ms/枚（遅くなるだけ） |
| `MALLOC_ARENA_MAX` / `MALLOC_TRIM_THRESHOLD_` / `MALLOC_MMAP_THRESHOLD_` | **効かない**（mimalloc が静的リンクされているので glibc を通らない） | — |
| `MIMALLOC_PURGE_DECOMMITS=1` / `MIMALLOC_ARENA_EAGER_COMMIT=0` | 増加は止まらない（`PURGE_DELAY=0` と同程度の後始末効果のみ） | — |
| `font: { loadSystemFonts: false }` | RSS の増え方は変わらない（4.763 MB/枚）。起動は少し速い | — |
| プロセスの再起動 | **試していない**（必要ないという結論になったため） | — |

API 面の注記: `@resvg/resvg-js` 2.6.2 の型定義には `Resvg` にも `RenderedImage` にも
明示的な `free()` / `dispose()` はない（`index.d.ts` で確認）。だから「イベントループを回す」以外の手がない。

---

## 4. 長時間動かすサーバーに置くとどうなるか

- **1 リクエスト 1 枚なら頭打ちになる。** ハンドラは必ず I/O で await するのでイベントループが毎回回り、
  1080×1080 の常駐は約 200〜230 MB（`MIMALLOC_PURGE_DELAY=0` なら約 196 MB）。2000 枚でこの水準、1 万枚でも同じはず。
- **危ないのは「1 リクエストで N 枚をループで作る」「バッチスクリプト」**。譲らないと 1 枚あたり
  幅×高さ×4 バイト（1080×1080 で 4.7 MB）で線形に増える。1 万枚なら単純計算で 47 GB に達し OOM する。
  ループの中に `await new Promise(r => setImmediate(r))` を 1 行入れるだけで消える。
- 同時実行 C 本なら常駐はだいたい `200 MB + C × 4.7 MB` になるはず（**未測定・推測**）。
- **フォントはプロセス起動時に 1 回読んで同じ Buffer を使い回すこと。** 毎回 `Buffer.from` すると
  1 枚あたり 9.3 MB の wasm 線形メモリが恒久的に積み上がり（縮まない）、数百枚で harfbuzz が異常終了する。
- したがってプロセスの定期再起動は（上の 2 点を守る限り）不要。守らなければ再起動でも隠しきれない（線形に増えるため）。

---

## 5. 成果物

- 測定スクリプト: `memory/trend.mjs`（本体）、`memory/lib.mjs`（スナップショット）、`memory/wasmhook.mjs`（`WebAssembly.Memory` の捕捉）
- 実行スクリプトとログ: `memory/run_all.sh` / `run2.sh` / `run3.sh` / `run4.sh` / `run5.sh` と同名の `.log`
- 生データ: `memory/out_*.json`（各サンプル点の rss / RssAnon / heapUsed / heapTotal / external / arrayBuffers / v8 統計 / smaps_rollup / wasm 内訳）
- 表: `memory/table.md`（`node table.mjs` で再生成）、個別の推移は `node summarize.mjs out_xxx.json`
- satori 0.33.5 のソース（読むために取得）: `memory/_satori_src/src/`
