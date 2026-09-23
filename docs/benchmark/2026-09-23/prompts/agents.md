# エージェントへの作業指示（2026-09-23。要点を残す。全文は当日の会話にある）

すべて opus のサブエージェント。生成側（A・B・D）にはリポジトリのソースを読ませていない。

## A: 普段の AI として HTML を書く（a_plain/）
- 「ふつうのアシスタント」として `requests.md` の 10 件に HTML で答える。**shashoku を知らない前提**。この会話のプロジェクト説明（CLAUDE.md）を無視し、そこに書かれた制限を考慮しない
- リポジトリと `exp/` の他のファイル（skill / ガイド）を読まない。描画・変換・検証をしない
- 依頼の内容を減らさない。普段どおりの技法（完全な HTML 文書、`<style>`、flex や grid、table、グラデーション、影、擬似要素、絵文字、Google Fonts）を、良くも悪くも普段どおりに
- 出力: `case01.html`〜`case10.html` と `NOTES.md`（想定サイズ・技法・前提）

## B: 対応範囲を渡された AI として書き、CLI で通す（b_guided/）
- 読む順: 共通ガイド → `skill_shashoku.md`（渡された唯一の技術情報）→ `requests.md`。ソースは読まない。エラーから学ぶのは可
- 最初から対応範囲に合わせて書く。項目・文言・サイズを減らさない。実現できないものは代替で表現し `dropped` に記録
- CLI で PNG 化。**1 回目の結果を直す前に記録**。修正ごとに `iterations` に追加。10 回で `gave_up`
- 通ったら PNG を Read で見て fidelity（1〜5）と組版の観察（行頭の句読点、約物、混植）を記録
- 追加フィールド: `skill_gaps`（文書に無くて困った情報）、`error_message_quality`

## C: 普段の HTML をエラーごとに最小修正して通す（c_fix/）
- 読む順: 共通ガイド → `skill_shashoku.md` → `a_plain/` の HTML と NOTES → `css_usage.json` → Chrome の参照画像
- 最小修正の規則: 外枠（DOCTYPE/html/head/meta/title/link/body）は外す（wrapper）／未対応タグは div・span に（tag）／未対応プロパティは hint に従い、無ければ削る（property）／未対応の値・単位は最も近い対応値に（value/unit）／未対応セレクタはクラスに、擬似要素の飾りは実要素で残す（selector）／絵文字は消さない（font）／見た目の作り直し・内容の削減・別デザインへの差し替えはしない
- 反復は 15 回まで（実際は測定成立のため上限を外し、`final_if_capped_at_15` も記録）。編集量は `diff | grep -c '^[+-]'`
- 通ったら参照画像と見比べて fidelity と `lost_visuals`。日本語組版の観察も
- 試算: 未対応を機械的に捨てる `strip.py`（外枠を外す／未対応タグ置換／未対応宣言と規則を削除）で通した結果と絵の質を `strip_result` に

## D: Satori 側（satori/）
- 導入（Node 20 を自前展開、`npm install satori satori-html @resvg/resvg-js`）の手順・量・時間を記録
- `render.mjs`（satori-html → satori → resvg）。フォントは同じ 3 本。画像は data URI
- 「Satori の README を渡された AI」として 10 件を Satori 向けに書く。項目・文言・サイズを減らさない。できないもの（縦書き・ルビ）は代替で表現し記録
- 未対応を黙って無視するので「エラーは出ないが期待どおりでない」も iteration に数える（`silent`）。1 回目の結果を直す前に記録
- 日本語の見え方（禁則、約物、折り返し、ふりがな、縦書き、混植、絵文字）を具体的な箇所で記録。禁則は狙って壊す試験（`tmp/kinsoku.html`）も
- cold（`/usr/bin/time -v` × 3）と warm（1 プロセスで 3 回捨てて 20 回）、RSS
- 追加作業: `a_plain/` の完全な HTML をそのまま通す。通らなければ機械的な前処理を 3 段まで。exit 0 なのに絵が壊れた数を数える

## メモリ調査（satori/memory/）
- 仮説 5 つ（V8 ヒープ／wasm／ネイティブ／フォントキャッシュ／頭打ちかリークか）をそれぞれ別プロセスの実測で白黒つける。`--expose-gc` で毎回 gc してから測る。satori だけ／resvg だけを分ける。glibc / mimalloc の環境変数を変えて比べる。必ず答える問い (a) 何が増えたか (b) satori と resvg のどちらか (c) リークか保持か (d) 回避策
- セッション終了で 1 度落ちたので、再開版には「途中結果を確認して再利用し、欠けた測定だけやり直す」「測定が 1 つ終わるたびに JSON と REPORT を書き足す」を追加

## 常駐・同時生成（perf2/）
- 3 条件（初回 1 枚＝測定済み／常駐／同時生成）に分けて、同じ入力・同じ機械で直列に測る
- shashoku: 公開 API だけを使う `bench.cpp`。`compile_commands.json` と `build.ninja` の CLI のリンク行からフラグとライブラリの並びを取る。常駐は 3 回捨てて 20 回、1000 枚連続で RSS。同時生成は `std::thread` N = 1/4/8/16 が `LoadedFonts` を共有
- Satori: メモリ調査の結論どおり 1 枚ごとに `setImmediate` で譲る。同時生成は `Promise.all`（1 プロセス）と `worker_threads`。VmHWM はプロセス全体
- 出力が同じ内容・寸法かを PNG ヘッダで確認して表に書く。言えること／言えないことを分ける

## 共通ガイドの要点（`EXPERIMENT_GUIDE.md`）
失敗を隠さない／リポジトリは読むだけ／GitHub への書き込み・設定変更をしない／権限の拒否に当たったら別の形で通そうとせず止まって報告／終了コードをパイプで隠さない／依頼文を書き換えない／記録は共通の `results.json` 形式（first_try、iterations、final、fidelity、dropped、warnings、notes）
