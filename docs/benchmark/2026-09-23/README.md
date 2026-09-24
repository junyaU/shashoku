# 2026-09-23 の検証資産（判断 A46・A47 の根拠を再現するためのもの）

要約は [../results_2026-09-23.md](../results_2026-09-23.md)、[../perf_resident_concurrent_2026-09-23.md](../perf_resident_concurrent_2026-09-23.md)、[../satori_memory_2026-09-23.md](../satori_memory_2026-09-23.md)。ここは入力・指示・スクリプト・生データ・代表画像。

| ディレクトリ | 中身 |
|---|---|
| `prompts/` | 依頼 10 件（`requests.md`）、対応範囲の文書（`skill_shashoku.md`。README 抜粋 111 行。生成エージェント B に渡した唯一の技術情報）、共通ガイド、各エージェントへの作業指示（`agents.md`） |
| `inputs/a_plain/` | 「普段の AI」が制限を知らずに書いた HTML 10 件と NOTES |
| `inputs/b_guided/` | 対応範囲を渡した AI が書いた HTML 10 件（全件 1 回で shashoku を通過） |
| `inputs/c_fix/` | a_plain をエラーごとに最小修正して通した最終版（`*_final.html`）と、未対応を機械的に捨てた試算版（`*_strip.html`） |
| `inputs/satori/` | Satori の README を渡した AI が書いた Satori 向け HTML 10 件と引数 |
| `inputs/grareco_shk.html` | 設計方針のグラレコを対応範囲だけで書き直したもの（900×5946 で通過） |
| `scripts/` | 静的解析（`css_usage.py`）、Chrome の参照画像（`chrome_shot.sh`。Windows 側 Chrome を WSL から呼ぶ）、cold 計測（`measure_shashoku.sh`）、Satori のレンダラと `package.json` / `package-lock.json` / Node の版、メモリ調査と常駐・同時生成の計測プログラム（`bench.cpp` は公開 API だけを使う） |
| `data/` | 各経路の `results.json`、cold / warm / 常駐 / 同時生成の計測 JSON、Satori のメモリ調査の生データ 35 本 |
| `images/` | 代表画像: b_guided の 10 枚、Satori の最終 10 枚と 1 回目の失敗例 4 枚（切れ・空白・ルビ連結・横書き）、c_fix の最終 4 枚と黙って無視した試算 1 枚（白地に白文字）、Chrome の参照 3 枚、shashoku で出したグラレコ |

条件: shashoku main c157766（release）、satori 0.33.5 / satori-html 0.3.2 / @resvg/resvg-js 2.6.2、Node 20.18.1、フォントは `build/_assets/fonts/` の Noto Sans JP Regular / Bold と Noto Sans、機械は i9-14900KF / WSL2 Ubuntu 22.04。
生成エージェントは opus 1 モデル。fidelity はエージェントの自己評価で、代表例のみオーケストレーターが目視した。人の修正時間は測っていない（反復 = エラー 1 件）。
