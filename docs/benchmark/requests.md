# 依頼集（機能追加の関門が参照する「対象用途」の実例）

DESIGN.md §2 の関門「対象用途で、使える PNG を得る成功率・修正の手間・運用上の信頼性を改善するか。
保守コストに見合う実例が依頼集にあるか」の**依頼集**がこのファイル（ARCHITECTURE.md A47）。

- 用途を足すときは依頼を足す。依頼は製品に合わせて選ばない（実装上の制限や技術の指定を含めない）
- 機能の採否は「依頼集の中で何件が困ったか、代替が無かったか」で判定する。「AI がどこかで使った」「誰かが欲しいと言った」は実例に数えない
- 検証のやり方（4 経路、Chrome の参照画像、fidelity の目視）と 2026-09-23 の結果は [results_2026-09-23.md](results_2026-09-23.md)

---

## case01 比較表（技術選定資料）
社内の技術選定資料に貼る比較表を HTML で作ってください。RDB（PostgreSQL）／KVS（Redis）／ドキュメント DB（MongoDB）の 3 つを、「得意なデータ」「トランザクション」「スケール」「運用の手間」「うちでの用途」の 5 項目で比べます。見出し行は色付き、行ごとに薄い縞模様。幅は 900px くらい。中身の文章は各セル 20〜40 字の日本語で、それらしく埋めてください。

## case02 処理フロー図（設計レビュー資料）
設計レビューの資料に貼る処理フローの図を HTML で作ってください。「ユーザー」→「API サーバー」→「キュー」→「ワーカー」→「DB」の 5 段を横一列に並べ、各段に 1〜2 行の説明（何をするか）を入れ、段の間に矢印を置いてください。ワーカーの下に「失敗時は 3 回まで再試行し、それでも失敗したら dead letter に送る」という注記を添えてください。幅 1000px。

## case03 用語カード（社内 wiki）
社内 wiki に貼る用語解説のカードを HTML で作ってください。用語は「冪等性」で、読み（べきとうせい）も表示します。定義を 2 文、具体例を 1 つ（「同じ注文 ID で決済 API を 2 回呼んでも、二重に課金されない」）、関連語 2 つ（リトライ、at-least-once）を小さいタグで。幅 720px、落ち着いた配色で。

## case04 手順カード（オンボーディング）
新人向けの開発環境セットアップ手順を 1 枚の画像にしたいので、HTML で作ってください。5 ステップ（リポジトリの clone、ツールチェーンの導入、依存ライブラリの取得、ビルド、テストの実行）。各ステップに番号の丸いバッジ、見出し、1〜2 行の補足説明。縦に並べます。幅 800px。

## case05 読書メモの引用カード（SNS）
SNS に投稿する読書メモ用の正方形カード（1080×1080）を HTML で作ってください。本文はこの一節です:
「読みやすい文章とは、読み手が息をつく場所が、書き手の意図した場所と一致している文章のことだ。句読点は、その約束のしるしにすぎない。」
書名『文章の呼吸』、著者名「佐藤 一葉」を右下に小さく（架空の書名・著者です）。余白を広めに取って、本の一節らしい落ち着いた雰囲気にしてください。

## case06 OG 画像（ブログ記事）
ブログ記事の OG 画像（1200×630）を HTML で作ってください。タイトルは「Rust で書き直したら速くなった、と言うために測ったこと、測らなかったこと」。サイト名「junya.dev」、著者アイコン（画像ファイルは avatar.png を用意します）、公開日 2026年9月23日、記事のタグ「Rust」「計測」。濃い色の背景に白い文字で。

## case07 イベント告知（勉強会）
勉強会の告知画像（1080×1080）を HTML で作ってください。
- イベント名: 第 12 回 組版と文字の勉強会
- 日時: 2026年10月8日（木）19:30〜21:00
- 場所: オンライン（Zoom）
- 登壇者 3 名（名前にふりがなを付けてください）:
  - 高橋 遥（たかはし はるか）「縦書き CSS の現在地」
  - Chen Wei（チェン ウェイ）「CJK フォントのサブセット化」
  - 佐々木 陽向（ささき ひなた）「ルビの自動生成」
- 「参加無料」のバッジ
- 申込: example.com/typeset12

## case08 通知画像（週次レポート）
Slack に自動投稿する週次レポートのカード画像を HTML で作ってください。見出し「今週のチーム活動（9/14〜9/20）」。3 つの指標を大きく: マージした PR 42 件（前週比 +8）、レビューコメント 156 件（前週比 −12）、デプロイ 9 回（前週比 ±0）。増えたものは緑、減ったものは赤で前週比を表示。下に一言「来週はリリース週です。木曜までに PR を出してください。」幅 800px、高さは内容に合わせて。

## case09 要約カード（ニュース）
ニュース記事の要約カードを HTML で作ってください。見出し「政府、AI 生成コンテンツの表示義務化を検討――2027 年度の法改正を視野に」、要点 3 つの箇条書き（各 40 字前後の日本語で、それらしく）、出典「架空新聞 2026-09-22」、タグ「政策」「AI」。幅 800px、白背景に薄い枠線。

## case10 短歌（縦書き、SNS）
短歌を縦書きで 1 首、画像にしたいので HTML で作ってください。
「ゆふぐれの　駅のホームに　立ちつくし　届かぬ返事を　もう一度読む」
作者名「架空 花」を左端に小さく添えてください。和紙のような薄い生成り色の背景に墨色の文字、余白は広めに。縦長（800×1200）。

---

# 第 2 節: 実利用で見つかった依頼（別枠）

上の 10 件は回帰検証用に固定する。実際の利用で見つかった依頼はここに足す（依頼文そのまま。出どころと日付を添える）。
判定はここも含めて行うが、10 件の合格率とは分けて数える。

## real01 図のラベル・数字と単位・タグを途中で折らないと指定したい
図の各段の名前や、表の「17 ms」、タグの中の文字が、枠の途中で 2 行に折れてしまいます。
ここは分割しないと指定したいです。

- **出どころ**: 2026-09-25、ローカルの skill（`docs/guide/SKILL.md` + ガイド）で shashoku 自身の
  紹介用の絵を試作したとき。依頼者はオーケストレーター（メインの Claude）
- **実例**: 6 段のパイプライン図（等分の `flex: 1 1 0` × 6 + 矢印）を `--width 1100` で描くと、
  段の名前「スタイル付きツリー」が `スタイル付きツ` / `リー` と折れた。和文は語の途中でも折れるので
  `&nbsp;`（U+00A0）では止められない。**幅を 1200 に広げ、名前を短くして回避した**
- **受け入れケース**: [2026-09-25-skill/pipeline.html](2026-09-25-skill/pipeline.html)。
  `--width 1100` で描くと名前が折れる（`--width 1200` では折れない）。`white-space: nowrap` があれば
  1100 でも折れずに描け、それでも入りきらなければ `warning[content-overflow]` で知らせる、が期待する挙動
- **判定**: まだ。関門（対象用途で成功率・修正の手間・信頼性のどれをどれだけ改善するか、
  保守コストに見合うか）に当てるには実例が 1 件しかない。**折り返さない結果として箱や紙面の幅を
  超えうる**ので、採るなら既存のはみ出し診断（`content-overflow`）との組で検証する
- 記録: 同じ試作の経緯と困りごと 3 つは [2026-09-25-skill/README.md](2026-09-25-skill/README.md)

**2026-09-25 A58 で対応（`white-space: nowrap`）。** 受け入れケースの結果: `pipeline.html` の `.name` に `white-space: nowrap` を足すと `--width 1100` で 6 段の名前がすべて 1 行になり（修正前は 2 つが折れていた）、警告 0 件。`--width 900` でもまだ収まり、`--width 800` で `warning[content-overflow]`（58.3px, right, 31:5）が出て `--strict` は exit 1（紙面の外に出た分は診断され、親の箱の中のはみ出しは診断されない）。

---

# 第 3 節: 英語の自然な依頼 5 件（言語非依存の確認。2026-09-23 追加、固定）

日本語固有の機能（ルビ・縦書き・約物）を使わない、英語圏の利用者が普通に頼みそうな依頼。依頼文は AI に渡すそのまま。
狙いは「英語でもガイドを渡せば 1 回で使える絵になるか」と「装飾を落とした絵で用途を満たすか」。組版の限界を探る依頼は入れない（第 4 節へ）。

## en01 Blog OG image (server-generated)
Make an HTML Open Graph image (1200×630) for a blog post. Title: "What we measured, and what we didn't, before claiming the Rust rewrite was faster". Site name "junya.dev", author avatar (an image file avatar.png will be provided), publish date September 23, 2026, tags "Rust" and "Benchmarks". Dark background, white text.

## en02 Pricing comparison table (docs)
Build an HTML comparison table for our pricing page draft: Free / Team / Enterprise across five rows (seats, projects, history retention, support, SSO). Colored header row, zebra striping, about 900px wide. Fill the cells with plausible short phrases (3–10 words).

## en03 Incident response flow (design review)
Create an HTML diagram of our incident response flow for a design review doc: Detect → Triage → Mitigate → Communicate → Postmortem, five steps in a horizontal row with a one- or two-line description under each and arrows between them. Under "Mitigate", add a note: "If rollback fails twice, page the on-call lead." Width 1000px.

## en04 Quote card (social, 1080×1080)
Make a square HTML quote card for social media with this passage: "A readable sentence is one where the reader breathes exactly where the writer intended. Punctuation is only the promise of that rhythm." Attribute it to the (fictional) book "The Breath of Prose" by Iris Calloway, small, bottom right. Generous margins, calm, literary feel.

## en05 Weekly digest card (server-generated notification)
Make an HTML card for a weekly engineering digest that a bot posts to Slack. Heading "This week in engineering (Sep 14–20)". Three big numbers: 42 PRs merged (+8 vs last week), 156 review comments (−12), 9 deploys (±0). Increases in green, decreases in red. A one-line note at the bottom: "Release week is next week. Please open your PRs by Thursday." Width 800px; height should fit the content.

---

# 第 4 節: 境界ケース（組版の限界を調べる。需要の測定と混ぜない）

需要ではなく機能の限界を探るための入力。合格率には数えない。見つかったものは「既知の制限」か機能候補として記録する。

- 空白を含まない長い URL（`https://example.com/a/very/long/path/...` 60 文字以上）を狭い箱に入れたとき（折り返しの規則、`overflow-wrap`）
- 長い英単語（例: "incomprehensibilities"）が行幅に収まらないとき（ハイフネーションが無い）
- 見出しの大文字化・スモールキャップ（`text-transform` / `font-variant` が無い）
- 数字と英字を含む見出しに `letter-spacing` を掛けたとき（「第 1 2 回」の英語版）
- 高さを固定した紙面からの本文のはみ出し（A46 の `ContentOverflow` の受け入れ例）
- 絵文字を含む英語の文（豆腐の警告）
