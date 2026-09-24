> 2026-09-23 夜のセッションで測った常駐・同時生成の記録をそのまま収めたもの。`EXP/…` はそのセッションのスクラッチ（/tmp。すでに無い可能性がある）。要約は [results_2026-09-23.md](results_2026-09-23.md) §5。

# shashoku と Satori + resvg の実行コスト — 常駐と同時生成

測定日 2026-09-23。両者を**直列に**（同時に走らせずに）測った。数字はすべて実測。

---

## 1. 条件

| 項目 | 値 |
|---|---|
| CPU | Intel Core i9-14900KF（16 コア / 32 スレッド。WSL2 から見えるのは 32 論理 CPU） |
| OS | Linux 5.15.167.4-microsoft-standard-WSL2 |
| RAM | 32 GB（測定中の空き 29 GB 以上） |
| 測定開始時の load average | 0.11〜0.15（1 分平均。他の重い処理なし） |
| shashoku | コミット `c157766`、`build/release`（clang-18 + libc++-18、`-O3 -DNDEBUG`）。計測プログラム `bench.cpp` は `-O2` |
| shashoku の PNG 圧縮 | `RenderOptions::compression_level` = 6（既定。zlib 1.3.2） |
| Satori | satori 0.33.5 / satori-html 0.3.2 / @resvg/resvg-js 2.6.2 / Node v20.18.1 |
| Satori の PNG 圧縮 | resvg-js の `asPng()` に圧縮レベルの指定口が無い（`index.d.ts` が `asPng(): Buffer`）ので既定のまま |
| フォント | 両者に同じ 3 本（NotoSansJP-Regular.otf 400 / NotoSansJP-Bold.otf 700 / NotoSans-Regular.ttf 400）。合計 9.5 MB |
| 入力 | 同じ 10 件の依頼から作った、それぞれの対応範囲に合わせた HTML（`EXP/b_guided/*.html` と `EXP/satori/*.html`） |
| Satori の正しい使い方 | フォント Buffer は起動時に 1 回。**1 枚ごとに `await new Promise(r => setImmediate(r))` で譲る**（`EXP/satori/memory/REPORT.md` の結論。譲らないと resvg の pixmap の解放が先送りされて RSS が 4.7 MB/枚で線形に増える） |

参考（この測定の対象外。既測。出典は `EXP/measure/shashoku_b.json` / `EXP/satori/perf.json`）:
**条件 1「初回の 1 枚」**は shashoku CLI 10〜30 ms・RSS 37〜47 MB、Satori 0.18〜0.24 s・177〜208 MB。

### 出力が同じ内容・同じ寸法か

両者の計測プログラムが吐いた PNG は、寸法・バイト数とも既存の成果物（`EXP/b_guided/*.png` / `EXP/satori/*.png`）と
**完全に一致**した（＝以前と同じ絵を出している）。ただし**両者どうしの寸法は 10 件中 5 件で違う**:

| ケース | shashoku | Satori | 一致 |
|---|---|---|---|
| case01 | 900x637 | 900x560 | ✗ |
| case02 | 1000x291 | 1000x345 | ✗ |
| case03 | 720x531 | 720x430 | ✗ |
| case04 | 800x654 | 800x500 | ✗ |
| case05 | 1080x1080 | 1080x1080 | ✓ |
| case06 | 1200x630 | 1200x630 | ✓ |
| case07 | 1080x1080 | 1080x1080 | ✓ |
| case08 | 800x385 | 800x385 | ✓ |
| case09 | 800x424 | 800x415 | ✗ |
| case10 | 800x1200 | 800x1200 | ✓ |

違う理由: shashoku は高さ未指定なら内容に追従し、Satori は高さの指定が要る（`args.txt` の値）。
さらに HTML の中身そのものが別（それぞれの対応範囲に合わせて書いたもの）なので、
**同じケース名でも「同じ仕事」ではない**。寸法が一致する 5 件でも画素の中身は同じではない。

---

## 2. 条件 2: 常駐して連続生成（初期化後の 1 枚あたり）

方法: 1 プロセス。shashoku は `LoadedFonts::prepare()` / `LoadedImages::prepare()` 済み、
Satori はフォント Buffer を起動時に 1 回読む。**ケースごとに 3 回捨てて 20 回測り、中央値**。

| ケース | 出力寸法（shashoku） | shashoku ms | 出力寸法（Satori） | Satori ms（譲る時間の外） | Satori ms（内） | 内訳 satori / resvg |
|---|---|---|---|---|---|---|
| case01 | 900x637 | **19.3** | 900x560 ※ | **28.9** | 29.3 | 15.6 / 13.3 |
| case02 | 1000x291 | **9.3** | 1000x345 ※ | **15.1** | 15.2 | 6.3 / 8.7 |
| case03 | 720x531 | **14.1** | 720x430 ※ | **11.9** | 12.3 | 3.5 / 8.4 |
| case04 | 800x654 | **18.4** | 800x500 ※ | **17.0** | 17.1 | 7.4 / 9.6 |
| case05 | 1080x1080 | **22.2** | 1080x1080 | **15.5** | 15.7 | 1.9 / 13.5 |
| case06 | 1200x630 | **15.2** | 1200x630 | **12.5** | 12.5 | 2.5 / 10.0 |
| case07 | 1080x1080 | **29.8** | 1080x1080 | **19.6** | 20.3 | 5.3 / 14.3 |
| case08 | 800x385 | **12.3** | 800x385 | **12.6** | 12.8 | 3.9 / 8.5 |
| case09 | 800x424 | **12.5** | 800x415 ※ | **15.3** | 15.5 | 4.8 / 10.5 |
| case10 | 800x1200 | **18.3** | 800x1200 | **17.2** | 17.6 | 6.0 / 11.2 |

※ = 出力寸法が両者で違うケース。

- 10 ケースの中央値の中央値: shashoku **16.8 ms** / Satori **15.4 ms**（譲る時間の外）、**15.6 ms**（内）。
- 譲る時間（`setImmediate` 1 回）は **0.0〜0.7 ms/枚**で、Satori の 1 枚あたりの時間をほとんど動かさない。
  RSS を平らにする代償は実質ゼロ。
- 再現性: 両者とも 2 回目の走行で全ケースが 1 回目の ±3% に入った
  （`raw/sh_resident_rep2.json` / `raw/satori_resident_rep2.json`）。

### メモリ（1 プロセスで 10 ケース × 23 枚 = 230 枚を描いたときの `/proc/self/status`）

| | 開始 | 初期化後 | 終了 | VmHWM |
|---|---|---|---|---|
| shashoku | 1.7 MB | 14.4 MB（`prepare()` 後） | **29.1 MB** | **33.2 MB** |
| Satori | 76.6 MB | 86.0 MB（フォント読み込み後） | **282.8 MB** | **282.8 MB** |

### shashoku の 1000 枚連続（case07 = 1080x1080）

`render()` 中央値 **29.8 ms**、1000 枚で 29.9 秒。RSS は 100 枚ごとに見て**1 バイトも動かなかった**:

| n | 1 | 100 | 200 | 300 | 400 | 500 | 600 | 700 | 800 | 900 | 1000 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| RSS | 29.1 MB | 29.1 | 29.1 | 29.1 | 29.1 | 29.1 | 29.1 | 29.1 | 29.1 | 29.1 | 29.1 MB |
| VmHWM | 35.6 MB | 35.6 | 35.6 | 35.6 | 35.6 | 35.6 | 35.6 | 35.6 | 35.6 | 35.6 | 35.6 MB |

Satori 側の同じ性質（譲れば頭打ち）は今回取り直していない。既測: 2000 枚で 83.8 → 218.6 MB、
後半の傾き -0.011 MB/枚（`EXP/satori/memory/out_server_long.json`）。

---

## 3. 条件 3: 同時生成

方法:
- **shashoku**: `std::thread` を N 本。**同じ `LoadedFonts` / `LoadedImages` を共有**
  （`include/shashoku/loaded_fonts.hpp` の「スレッドの約束」— `prepare()` 後は完全に読み取り専用で、
  同時に何本の `render()` から参照してもよい。破棄とムーブ代入だけ利用者が直列化する）。
  各スレッドが 10 ケースを順に 5 周（50 枚）。
- **Satori (a) Promise.all**: 1 プロセスで N 本の並行チェーン。各チェーンが 50 枚。
- **Satori (b) worker_threads**: N 本のワーカー。**各ワーカーが自分でフォントを読む**。各ワーカーが 50 枚。

いずれも、全員がウォームアップ（10 ケース 1 周）を終えてから計測を開始。N ごとに**別プロセスで 3 回**測って中央値
（VmHWM は減らないので、N をまたいで汚さないため別プロセスにした）。

| N | shashoku 枚/秒 | shashoku VmHWM | Satori Promise.all 枚/秒 | その VmHWM | Satori worker_threads 枚/秒 | その VmHWM |
|---|---|---|---|---|---|---|
| 1 | **57** | 45.2 MB | **55** | 245.5 MB | **55** | 270.9 MB |
| 4 | **221** | 94.5 MB | **57** | 334.8 MB | **200** | 860.5 MB |
| 8 | **369** | 161.8 MB | **59** | 359.9 MB | **280** | 1474.2 MB |
| 16 | **558** | 294.8 MB | **59** | 431.1 MB | **395** | 2615.8 MB |

- N=1 → 16 の倍率: shashoku **9.8 倍**、Satori worker_threads **7.2 倍**、Satori Promise.all **1.07 倍**。
- 同時実行 1 本を増やすのにかかるメモリ（N=1 と N=16 の VmHWM の差 ÷ 15）:
  shashoku **16.6 MB/本**、Satori worker_threads **156 MB/本**。
- Promise.all は枚/秒が増えないのに VmHWM は 246 → 431 MB に増える。
  同時に飛んでいる N 枚ぶんの pixmap を抱えるため。

---

## 4. 読み方

### 実測から言えること

1. **1 枚あたりの時間は、常駐させればどちらも同じ桁**。中央値の中央値で shashoku 16.8 ms / Satori 15.4 ms。
   ケースごとには勝ち負けが混ざる（shashoku が速い 4 件、Satori が速い 6 件）。
   条件 1 で見えた 10〜20 倍の差（10〜30 ms 対 0.18〜0.24 s）は**ほぼ全部が起動と初期化のコスト**で、
   常駐すれば消える。「Satori は遅い」は初回だけの話。
2. **メモリの差は常駐しても消えない**。同じ 230 枚を描いたあとの RSS が 29.1 MB 対 282.8 MB（約 9.7 倍）、
   VmHWM が 33.2 MB 対 282.8 MB（約 8.5 倍）。Satori 側の内訳は既測で、
   V8 ヒープ約 35 MB + wasm 26.9 MB（harfbuzz 11.27 + yoga 16.78）+ Node 自体の約 80 MB
   （`EXP/satori/memory/REPORT.md`）。
3. **shashoku の RSS は 1000 枚で動かない**（29.1 MB のまま、VmHWM も 35.6 MB のまま）。
   常駐プロセスに置いたときの上限が読める。
4. **Node は 1 プロセスでは同時生成で処理量が増えない**。Promise.all に 16 枚まとめて投げても 59 枚/秒で、
   1 本のときの 55 枚/秒とほぼ同じ。satori も resvg の `render()` も同期的に CPU を使うので、
   イベントループ 1 本ぶんしか働かない。**並行にしたぶんメモリだけ増える**（246 → 431 MB）。
5. **Node で処理量を増やすには worker_threads が要り、そのぶんメモリが線形に増える**。
   ワーカーはフォント（9.5 MB）も wasm（26.9 MB）も V8 ヒープも**共有できない**ので、
   1 本増やすのに 156 MB かかる。shashoku は `LoadedFonts` / `LoadedImages` を全スレッドで共有できるので 16.6 MB。
   N=16 で 2.6 GB 対 295 MB（約 8.9 倍）。
6. **同じ枚数/秒を出すのに要るメモリ**（N=16 の実測から）: shashoku は 558 枚/秒を 295 MB で、
   Satori は 395 枚/秒を 2.6 GB で出す。1 枚/秒あたり 0.53 MB 対 6.6 MB。
7. **`setImmediate` で譲る代償は小さい**（0.0〜0.7 ms/枚）。Satori を常駐させるなら必ず入れるべきで、
   入れないと 4.7 MB/枚で RSS が伸びる（既測）。この測定はすべて譲った状態で行った。

### 言えないこと

- **1 枚あたりの時間の優劣は「同じ仕事」の比較ではない**。10 件中 5 件は出力寸法が違い、
  残り 5 件も HTML の中身が別（それぞれの対応範囲に合わせて書いたもの）で画素の中身も違う。
  PNG のバイト数も両者で違う（例 case01: 142,653 B 対 81,381 B）ので、
  エンコードの仕事量も同じではない。**「shashoku の方が N% 速い/遅い」は言えない。**
- **PNG の圧縮の強さを揃えられていない**。shashoku は zlib レベル 6 と分かっているが、
  resvg-js の `asPng()` には指定口が無く、実際のレベルは確認していない。
  PNG エンコードは shashoku の `render()` の大きな割合を占める（既測: 約 90%）ので、
  ここを揃えずに合計時間だけを比べることの限界がある。
- **case06 の画像の扱いが違う**。shashoku は `LoadedImages` にデコード済みで持ち（起動時に 1 回）、
  Satori は data URI を毎回デコードする。64x64 の小さい画像なので影響は小さいと思われるが、測っていない。
- **スレッドの伸びが飽和する理由を切り分けていない**。i9-14900KF は P コアと E コアが混ざっていて、
  WSL2 越しでスケジューリングが見えない。N=16 での 9.8 倍 / 7.2 倍という数字を
  「shashoku の方が並列化効率が良い」と読むのは早い（測っていない）。
- **リクエスト単位の待ち時間（p50 / p99）を測っていない**。測ったのは処理量（枚/秒）だけ。
  Promise.all の N=16 では 1 枚あたりの待ち時間は 16 倍近くに伸びているはずだが、記録していない。
- **プロセスプール（`cluster` / 複数プロセス）の Satori を測っていない**。
  worker_threads より当然メモリが重くなる向きだが、実測していない。
- **これは 1 台・WSL2 での測定**。コンテナや別の CPU での再現は確かめていない。

---

## 5. 出典

| 内容 | パス |
|---|---|
| shashoku 常駐（20 回 × 10 ケース、1000 枚連続、2 回目の走行） | `EXP/perf2/shashoku_resident.json` |
| shashoku 1000 枚連続（元データ） | `EXP/perf2/shashoku_longrun.json` |
| shashoku 同時生成（N × 3 回） | `EXP/perf2/shashoku_concurrent.json`（個別は `EXP/perf2/raw/sh_conc_<N>_<rep>.json`） |
| Satori 常駐（20 回 × 10 ケース、2 回目の走行） | `EXP/perf2/satori_resident.json` |
| Satori 同時生成（Promise.all / worker_threads、N × 3 回） | `EXP/perf2/satori_concurrent.json`（個別は `EXP/perf2/raw/sa_{promise,worker}_<N>_<rep>.json`） |
| 計測プログラム（C++、公開 API のみ） | `EXP/perf2/bench.cpp` |
| 計測スクリプト（Node） | `EXP/perf2/bench_satori.mjs` |
| 条件 1（初回の 1 枚。既測、今回は再測定せず） | `EXP/measure/shashoku_b.json` / `EXP/satori/perf.json` |
| Satori の RSS が戻らない件の切り分け（既測） | `EXP/satori/memory/REPORT.md` |

`EXP` = `/tmp/claude-1000/-home-junya-src-github-com-junyaU-shashoku/94c9fae6-a9a0-40c9-ac43-94fa14abccb2/scratchpad/exp`
