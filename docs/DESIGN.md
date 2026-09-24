# shashoku（写植）— 日本語組版特化 HTML→PNG レンダリングエンジン 設計書

> Claude Code に渡す前提のプロジェクト定義書。設計思想・スコープ・アーキテクチャ・フェーズ計画のすべてをここに集約する。
> 名前の由来: 写真植字機（写植）— 日本語の活字を組み、画像として出力していた機械。本プロジェクトはその再発明にあたる。

---

## 0. 一文ミッション

**AI とアプリのための、ブラウザ不要の HTML→PNG エンジン。文章主体のカード・図解・資料画像を生成し、問題は修正できる形で伝える。日本語組版にも強い。**

（2026-09-23 に 2 度書き換えた。ARCHITECTURE.md A47 とその追記。朝までは「日本語の文章を絶対に破綻させずに、HTML から PNG を一発で生成する組版エンジン」。「絶対に破綻させない」は検証しきれない約束なので避け、対象を言語で絞るのをやめた。範囲を抑えるのは用途と §2 の関門）

| 層 | 位置づけ |
|---|---|
| 製品の目的 | AI とアプリのための、ブラウザ不要の HTML→PNG。文章主体のカード・図解・資料画像 |
| 中核原則 | 対応範囲を明示し、検出した問題を黙って通さず、修正可能な診断を返す（§3-6）。**設計上の目標と現在の機能は区別する**: 一括報告・はみ出しの警告・JSON 診断・strict は A46 で設計し、実装済み（feature/a46）。未検出のレイアウトの誤り・重なり・文字色と背景の同化は今も対象外 |
| 対象と検証 | **日本語・英語を対象に検証する**（全言語の組版保証ではない。将来の対象は閉じない）。最初の試用は日本の開発者から（連絡とフィードバックの得やすさで決めた順序であり、対象の限定ではない） |
| 積み上げた強み | 日本語組版（両端揃え・約物の詰め・和欧文アキ・ルビ・縦書き）。既存機能として維持し、追加開発は他の機能と同じく需要で判断する |
| 当面追わないもの | 任意の Web ページの再現、JavaScript 実行、ブラウザ全体との互換性 |

差別化の中心は「ガイドが付属すること」ではなく、**ガイドとエンジンを一緒に設計し、生成・診断・修正まで一連で動くこと**: ガイドで最初から対応範囲内の HTML を書ける／外れたら位置と修正方法を含む診断を返す／欠落やはみ出しも検出して自動処理側が止められる／この一連の成功率をエンジンの更新ごとに検証する。「AI 向けガイドは他に無い」は前提にしない（Vercel は AI 向け skill をまとめて提供している）。
Satori が「flexbox で作ったカードを SVG にする汎用エンジン」だとすれば、shashoku は「対応範囲を明示して PNG まで出し切り、直せる形で失敗するエンジン」。想定ユーザーは、AI に HTML を書かせて画像にしたい開発者と、サーバーでカード類を自動生成するアプリ。
配布するものは 3 つ: 対応範囲と書き方のガイド（開発用の AI にも、サーバー上の LLM にもテキストで渡せる）、skill（そのガイドと CLI を開発用エージェントから使いやすくする包み）、サーバー連携の小さな実例（CLI 呼び出し・診断 JSON・上限つき再試行）。ガイドとエンジンの版はそろえる。修正ループの制御は呼び出し側のアプリの責任で、shashoku 自身は AI を呼ばない。

## 1. 背景 / 解決する課題

サーバーサイドでの HTML→画像変換（OG 画像生成が最大の用途）で広く使われている選択肢は主に 2 つ（ほかの変換器もあるが、ここでは比較しない）：

1. **headless Chrome (Playwright/Puppeteer)** — 何でも描けるが、数百 MB のバイナリと依存地獄。リクエスト毎の生成には重すぎる
2. **Satori (+resvg)** — 軽量だが、日本語組版と、サーバーで使うときの手当てが弱い：
   - **行の後始末がない**: 禁則の基本（行頭の句読点・閉じ括弧、行末の開き括弧、小書きかな）は UAX #14 で行う（2026-09-23 に Satori 0.33.5 で確認。以前この文書は「禁則処理がない」と書いていたが誤り）。無いのはその先で、両端揃え・約物の詰め・和欧文間のアキが無く、行末が不揃いになり括弧や句読点の空きが二重になる
   - **紙面の高さが必須で、内容に合わせられない**: 本文の長さで高さが決まる画像では、切れるか余るかのどちらかになる（2026-09-23 の実測で該当 6 件が 6 件とも）
   - **未対応を黙って捨てる**: `<ruby>` は読みが親文字に連結され、`writing-mode` は消え、それでも exit 0 で PNG が出る
   - **縦書き非対応**: `writing-mode: vertical-rl` が存在しない。小説・短歌・書籍系サービスは門前払い
   - **ルビ非対応**: `<ruby>` が使えない
   - **フォント運用が毎回手作り**: フォールバック連鎖・サブセット・豆腐対策をユーザーが配管する
   - **SVG→PNG の 2 段構成**: satori + resvg-js + フォント二重管理の配管工事、障害時の切り分けコスト

OG 画像や通知カードは動的な文字列を流し込むものなので、**任意の日本語文字列でも読める組版になり、収まらなければ黙らずに伝えること**が本質要件。ここを押さえたエンジンは無い。shashoku はこの穴を埋める。

## 2. ポジショニング

### 特化する（Satori が弱い・持たないもの）

| # | 機能 | 内容 |
|---|------|------|
| 1 | **行分割器（製品のコア）** | UAX #14（Unicode 行分割）＋ JIS X 4051（日本語組版規則）準拠。行頭禁則・行末禁則。あふれ処理は追い出し / 追い込み / ぶら下げをオプションで選択可能 |
| 2 | **縦書き** | `writing-mode: vertical-rl`、縦中横、`vert` フィーチャーによる約物グリフ差し替え |
| 3 | **ルビ** | `<ruby>` `<rt>` 対応 |
| 4 | **フォント運用の内蔵** | 日本語向けフォールバック連鎖の仕組み、グリフ欠落（豆腐）の検出とレポート |
| 5 | **PNG 直出し** | 関数 1 個で PNG バイト列が返る。中間 SVG なし、外部ツールなし |

### 削る（Satori が持っていても捨てる）

1. **CJK 以外の複雑スクリプト**（アラビア語の文脈変形等）は非対応と明言する
2. **grid / float / table** — レイアウトは block + inline + flexbox のみ
3. **JS 実行・外部リソース取得** — 画像はバイト列で受け取る。ネットワークに一切触れない純粋関数
4. **SVG 主出力** — デバッグダンプに格下げ（後述の `--dump-stage` の一形態）
5. **エッジ JS 環境ファースト** — まずネイティブで正しさと速さ。バインディングは需要が見えてから

### 個性を支える態度

**未対応は黙って崩さず、エラーで落とす（fail loudly）。**
Satori 系への不満は「対応外の CSS を書くと無言で変な絵が出る」こと。shashoku は `float: left` を見たら即座に「非対応プロパティ」エラーを返す。対応範囲の狭さを隠さず、範囲内の品質を保証する。この態度そのものが差別化。

これは守りではなく攻めの機能でもある。**HTML を AI が書き、結果をサーバーが処理する前提では、機械が読める診断が修正と配信の判断を支える。**
対応範囲の文書・成功例・画像の目視もフィードバックだが、エラーと警告は最も自動処理しやすい経路である。
2026-09-23 の実測では、普段どおりに AI が書いた HTML が shashoku で止まった 180 件のうち 170 件はエラー文を読めば直せたが、
1 回に 1 件しか出さなかったため CLI が 190 回走った。逆に Satori は同じ依頼で exit 0 のまま壊れた絵を 9/10 返した。
だからエラーと警告は「一度に全部・直し方つき・結果にも及ぶ」形で出す（§3-6、ARCHITECTURE.md A46）。
ただし約束の範囲は正確に言う: 成功が意味するのは「対応範囲の中で、検出対象の検査を満たした」ことであって、絵が正しいことの保証ではない。

### 機能追加の判定基準（迷ったらこの二問）

**「対象用途で、使える PNG を得る成功率・修正の手間・運用上の信頼性のどれかを改善するか？　保守コストに見合う実例が依頼集にあるか？」**
（2026-09-23 に書き換え。ARCHITECTURE.md A47。以前は「日本語の文章を正しく組むことに寄与するか」）

- **対象用途と実例は固定した依頼集で測る**: [benchmark/requests.md](benchmark/requests.md)。用途を足すときは依頼を足す。「AI がどこかで使った」「誰かが欲しいと言った」は実例に数えない。依頼集の中で何件が困ったか、代替が無かったか、で判定する
- 判定は「通る／通らない」の二値ではなく、成功率・修正の手間・信頼性のどれにどれだけ効くかと保守コストの比較で、**現時点の優先度**として言う
- 優先度が高い例: 辺ごとの `border`（依頼集 10 件中 8 件で使われ、削ると表の罫・区切りが失われて代替が無い）、エラーの一括報告（修正の手間）、はみ出しの警告（運用上の信頼性）、ルビ・縦書き（依頼集の用途で要る）
- 現時点では優先度が低い例: グラデーション（普段の AI の HTML では 10/10 が使うが、対応範囲を渡した AI は単色で目的を満たせた。修正の手間は hint で 1 回。「背景はグラデーション」の依頼が依頼集に入れば再判定）。追わない例: 任意の HTML の無修正変換、JavaScript
- **既存 10 件は回帰検証用に固定**し、実利用で見つかった依頼は別枠として追加する。製品の成長を 10 件に閉じ込めない
- 日本語組版の機能もこの関門を通す。「日本語だから入れる」ではなく「依頼集の用途で要る」で判定する

## 3. 設計原則

1. **一方向パイプライン**: 意味 → 座標 → ピクセル への片道変換。各段は前段の出力だけを入力とし、後戻りしない。段が終われば前段の中間表現は破棄してよい（静的一発変換であり、ブラウザのような差分再計算はしない）
2. **重いデータは参照で引き回す**: グリフ輪郭・フォント実体は FontStore が所有し、ツリーには ID（FontId, glyph_id）だけを載せる。デリファレンスはラスタライズの瞬間のみ
3. **各段の出力はダンプ可能**: `--dump-stage=dom|style|box|display-list|svg` で全中間表現を目視できる。デバッグとテストの基盤
4. **行分割器は独立モジュール**: 製品のコアなので、レイアウトエンジンから分離してテーブル駆動で単体テストできる形にする。テキスト計測器（TextMeasurer）はインターフェースとしてレイアウトに注入する（レイアウト⇄計測の相互再帰を疎結合に保つ）
5. **純粋関数**: 同じ入力（HTML + フォント + オプション）からは常にバイト単位で同じ PNG が出る。グローバル状態・時刻・乱数・ネットワークへの依存なし。**この一致をどの範囲で保証するか**（同じ版・同じ依存・x86-64 Linux の 2 つのツールチェーンまでは検査済み／aarch64・macOS・MSVC・依存の版違いは未確認）は [README の「決定性」](../README.md#決定性同じ入力から同じ-png)にまとめる。原則はここに書いたとおりで変わらない
6. **fail loudly、直せる形で**: 未対応のタグ・プロパティ・値、欠落、はみ出しを黙って通さない。エラーと警告は AI（と人）が読んで直すためのフィードバックであり、製品の主機能の 1 つとして扱う。約束は 3 つ。**(1) 一度に全部**: 最初の 1 件で止めず、入力にある問題をまとめて返す。**(2) 直し方つき**: 入力位置と「何がだめか」に加えて「代わりにどう書くか」を、shashoku で同じ結果が出せると確かめた範囲で添える。**(3) 構文だけでなく結果にも**: 対応外の記法だけでなく、組み上がった絵の欠落（豆腐、固定した紙面からのはみ出し、画像の不在）も報告する。豆腐とはみ出しは警告として返して描画は続行するが、利用者は警告をエラーに格上げできる（サーバーで「配らない」判断に使う）。「未対応を黙って無視するモード」は入れない（試算では 5/10 が通らず、通った 5 件中 4 件で文字が背景と同色になって消えた）。ARCHITECTURE.md A46。**現状（2026-09-23）**: (1)〜(3) は実装済み（feature/a46）。①② の一括報告、`RenderError::hint`、紙面からのはみ出しの警告、`RenderOptions::warnings_as_errors`（CLI は `--strict`）、`--diagnostics json` が入っている。**約束の範囲は限定して言う**: strict の成功が意味するのは「対応範囲の中で、検出対象（対応外・豆腐・紙面のはみ出し・画像の不在）の検査を満たした」ことで、未検出のレイアウトの誤り・重なり・文字色と背景の同化は対象外

## 4. スコープ

### 対応する入力

- HTML サブセット: `div`, `span`, `p`, `h1`-`h6`, `img`, `ruby`, `rt`, `br`, テキストノード
- スタイル指定: `style` 属性（Phase 5 で `<style>` + 単純セレクタ tag / .class / #id を追加）
- 文字コード: UTF-8 のみ

### 対応 CSS プロパティ（初期セット）

```
display (block | flex | inline | none)
box-sizing (content-box | border-box)
width, height, margin*, padding*
font-size, font-family, font-weight, line-height, color
background-color, border, border-radius
text-align, letter-spacing
flex-direction, justify-content, align-items, gap, flex-grow/shrink/basis
line-break (auto | strict | loose), overflow-wrap (word-wrap は legacy name alias)
writing-mode (horizontal-tb | vertical-rl)  ← Phase 8
```

これ以外のプロパティは **エラー**（unsupported property）。値の単位は px と em と %（幅のみ）から始める。

### 対応しない（non-goals、README に明記する）

- JavaScript の実行
- 外部リソースの取得（URL 参照の画像・Web フォント。すべてバイト列で渡してもらう）
- grid / float / table / position:fixed / アニメーション / メディアクエリ
- CJK 以外の複雑スクリプトのシェーピング品質保証
- ブラウザとのピクセル一致（目標は「正しい日本語組版」であり「Chrome の再現」ではない）
- インクリメンタル再レイアウト（毎回フル変換。ブラウザではないので不要）

## 5. アーキテクチャ: 6 段パイプライン

```
HTML文字列
  │ ① パース（トークナイズ → 木構築）
  ▼
DOMツリー（意味と構造。座標なし）
  │ ② スタイル解決（宣言パース → カスケード → 継承 → 計算値化）
  ▼
スタイル付きツリー（全ノードに ComputedStyle が確定。1.2em や未指定が消えた状態）
  │ ③ レイアウト ★本丸
  │    ・ボックス生成（1ノード→0..N 長方形。テキストは行フラグメントに分裂）
  │    ・幅は親から子へ降り、高さは子から親へ戻る
  │    ・行分割器がここで動く（禁則処理の住所）
  │    ・行分割は文字幅を要求 → TextMeasurer 経由で ④ を呼ぶ（相互再帰）
  ▼
ボックスツリー（xywh 確定。行ボックスにはグリフ参照＋座標が埋まっている）
  │ ⑤a 木を平らな命令列に潰す
  ▼
ディスプレイリスト（FillRect / DrawGlyphs / DrawImage / PushClip... の列）
  │ ⑤b ラスタライズ（輪郭 → 被覆率 → アルファ合成）
  ▼
RGBAピクセルバッファ（width × height × 4 バイトの配列）
  │ ⑥ エンコード（スキャンラインフィルタ → zlib → チャンク）
  ▼
PNGバイト列
```

### 主要データ構造（C++ スケッチ）

```cpp
// ① DOM
struct Node {
  enum class Type { Element, Text };
  Type type;
  std::string tag;                                  // Element のみ
  std::vector<std::pair<std::string, std::string>> attrs;
  std::string text;                                 // Text のみ
  std::vector<Node> children;
};

// ② 計算値（全プロパティが必ず埋まる。未指定は initial 値か継承値）
struct ComputedStyle {
  Display display;
  float width;          // 解決済み px（auto は NaN 等の番兵でなく optional で表現）
  float font_size;      // em は解決済み
  FontStack font_family;
  LineBreakPolicy line_break;
  WritingMode writing_mode;
  // ...
};

// ③ ボックスツリー
struct PositionedGlyph { uint16_t glyph_id; float x; };
struct TextFragment {
  FontId font;
  float baseline_y;
  std::vector<PositionedGlyph> glyphs;
};
struct LineBox { Rect rect; std::vector<TextFragment> fragments; };
struct BlockBox { Rect rect; Style paint_style; std::vector<BoxVariant> children; };

// フォント管理（実体の唯一の所有者）
class FontStore {
 public:
  FontId load(std::span<const uint8_t> font_bytes);
  // cmap 引き。フォールバック連鎖を辿り、どのフォントで描くかを返す
  ResolvedGlyph resolve(char32_t cp, const FontStack&, Orientation);
  float advance(FontId, uint16_t glyph_id, float font_size);
  Outline outline(FontId, uint16_t glyph_id);       // ⑤b だけが呼ぶ
};

// レイアウトに注入する計測インターフェース（相互再帰の絶縁層）
class TextMeasurer {
 public:
  virtual ShapedRun shape(std::u32string_view, const ComputedStyle&) = 0;
  virtual ~TextMeasurer() = default;
};

// ⑤a ディスプレイリスト
using DrawCmd = std::variant<FillRect, FillRoundedRect, DrawGlyphs, DrawImage,
                             PushClip, PopClip>;
using DisplayList = std::vector<DrawCmd>;

// ⑤b 出力
struct Bitmap { uint32_t width, height; std::vector<uint8_t> rgba; };
```

### コアモジュール: 行分割器（LineBreaker）

```cpp
// 入力: グリフ列（幅つき）と利用可能幅。出力: 行への分割位置
// UAX #14 で候補点を出し、JIS X 4051 の禁則で候補を潰し、
// あふれたら OverflowPolicy に従って調整する
enum class OverflowPolicy {
  Oidashi,   // 追い出し: 禁則文字を道連れに次行へ（デフォルト）
  Oikomi,    // 追い込み: 字間を詰めて行内に収める
  Burasage,  // ぶら下げ: 句読点を行末からはみ出させる
};

class LineBreaker {
 public:
  std::vector<LineRange> break_lines(std::span<const MeasuredChar> chars,
                                     float available_width,
                                     const LineBreakConfig& cfg);
};
```

- **禁則文字クラスはテーブルで持つ**（行頭禁則: `。、」』）〕｝〉》ゃゅょっー…` など / 行末禁則: `「『（〔｛〈《` など）。`line-break: strict / loose` でテーブルを切り替える
- ICU には依存しない。日本語＋基本ラテンに必要な UAX #14 のサブセットを自前実装する（ここが製品価値なので外注しない）
- レイアウトエンジンから完全に独立させ、テーブル駆動の単体テストを大量に書く

## 6. 日本語組版仕様（コアの詳細）

1. **禁則処理**: 上記 LineBreaker。行頭禁則・行末禁則・分離禁則（`——` `……` の途中で割らない）
2. **ぶら下げ組み**: 句読点 1 文字ぶんだけ行末からのはみ出しを許す。はみ出しにより行数が変わり、親の高さが変わることに注意（レイアウト結果そのものが変わる）
3. **縦書き（Phase 8）**: 主軸の転置（幅と高さの役割交換）＋ HarfBuzz の `vert` フィーチャーで約物グリフを差し替え＋縦中横（`text-combine-upright` 相当は将来）
4. **ルビ（Phase 7）**: 親文字の上（縦書きなら右）に小サイズのグリフ列を配置。行高への影響（ルビぶんの行間確保）を含む
5. **フォントフォールバック**: `FontStack` を順に cmap 引きし、最初にグリフを持つフォントを採用。テキストは「同一フォントで描ける区間（run）」に分割されてからシェーピングされる
6. **豆腐検出**: どのフォントにもグリフがないコードポイントは警告リスト（コードポイント＋位置）として `RenderResult` に積み、□ を描画して続行する。
   位置は「その文字を含むテキストノードの先頭」で、報告は (コードポイント, テキストノード) の組ごとに 1 件。
   並びは入力位置の昇順 → コードポイントの昇順（ARCHITECTURE.md A31）

## 7. 技術選定（C++）

- **言語 / 規格**: C++23（`std::expected` をエラー型に使う）。例外はライブラリ境界で使わない
- **ビルド**: CMake + FetchContent。フォーマットは clang-format、静的解析に clang-tidy
- **テスト**: GoogleTest

### 自作する / 借りる の線引き

| 部品 | 方針 | 理由 |
|------|------|------|
| HTML パーサ | **自作** | 対応タグが少なくサブセットで足りる。仕様(WHATWG)の該当部分だけ読めば書ける |
| CSS パーサ＋カスケード | **自作** | style 属性＋単純セレクタなら小さい。計算値化のロジックが学びどころ |
| レイアウトエンジン | **自作（絶対）** | 製品のコア。taffy/Yoga 相当の flexbox も自分で書く |
| 行分割器 | **自作（絶対）** | 製品価値そのもの。ICU に頼らない |
| 文字→グリフ（cmap/hmtx 読み） | **FreeType** | フォントバイナリの読み出しは枯れたライブラリに任せ、メトリクスの解釈と運用（フォールバック・豆腐検出）に集中する |
| シェーピング（vert 差し替え等） | **HarfBuzz** | 自作は無謀の代表格。縦書きに必須 |
| グリフのラスタライズ | **まず FreeType（FT_Render_Glyph）** | Phase 前半はビットマップをもらって合成に集中。被覆率 AA の自作ラスタライザは Phase 6 以降の差し替え課題として残す（インターフェースを切っておく） |
| 矩形・角丸・合成 | **自作** | source-over 合成と矩形塗りは小さい。角丸は自作スキャンラインの練習台 |
| zlib (deflate) | **zlib or miniz** | 圧縮アルゴリズム自体は本題でない |
| PNG エンコーダ | **自作** | チャンク構造＋フィルタ＋CRC32 で 200 行程度。Phase 0 の題材として最適 |
| JPG エンコーダ | **libjpeg-turbo（後回し）** | テキスト画像に JPG は不向きなので優先度低 |

## 8. 公開 API

```cpp
namespace shashoku {

struct RenderOptions {
  int viewport_width = 1200;         // OG 画像の定番サイズをデフォルトに
  std::optional<int> viewport_height; // 未指定ならコンテンツ高さに追従
  float scale = 1.0f;                // 2.0 で Retina 向け 2 倍解像度
  LineBreakConfig line_break;        // 禁則テーブル・OverflowPolicy
  RenderLimits limits;               // 入力の上限（バイト数・ノード数・font-size・画素数…）
  int compression_level = 6;         // PNG（zlib）の圧縮レベル 0〜9。範囲外は InvalidOption
};

// 処理全体の予算。上限は「入力の一部」なので、同じ入力 + 同じ上限なら出力も同じ。
// 既定値は OG 画像には十分広く、事故（巨大な font-size、画像の枚数、深い入れ子）は止まる。
// 超過は ErrorKind::LimitExceeded。詳細は ARCHITECTURE.md A25。
struct RenderLimits { /* html_bytes, dom_nodes, text_code_points, font_size_device_px, … */ };

// 位置はその文字を含むテキストノードの先頭（ARCHITECTURE.md A31）
struct Warning { WarningKind kind; std::string detail; char32_t codepoint;
                 std::optional<SourceLocation> location; };

struct RenderResult {
  std::vector<uint8_t> png;
  std::vector<Warning> warnings;     // 豆腐など、続行可能な問題
};

class FontSet {
 public:
  void add(std::span<const uint8_t> font_bytes);  // 追加順がフォールバック順
};

// 唯一のエントリポイント。純粋関数
std::expected<RenderResult, RenderError>
render(std::string_view html, const FontSet& fonts, const RenderOptions& opts);

// 連続生成のための共有資源（ARCHITECTURE.md A34）。バイト列の解釈と PNG のデコードを
// 1 回だけ済ませて使い回す。prepare() のあとは読み取り専用で、複数の render() から、
// 複数のスレッドから同時に使ってよい（破棄とムーブ代入だけは利用者が直列化する）。
// **出力は変わらない**: 毎回 FontSet / ImageSet から作り直したのとバイト単位で同じ PNG が出る。
class LoadedFonts  { public: static std::expected<LoadedFonts, RenderError>  prepare(const FontSet&); };
class LoadedImages { public: static std::expected<LoadedImages, RenderError> prepare(const ImageSet&, const RenderLimits& = {}); };

std::expected<RenderResult, RenderError>
render(std::string_view html, const LoadedFonts& fonts, const LoadedImages& images,
       const RenderOptions& opts);

}  // namespace shashoku
```

- エラー（`RenderError`）: パース失敗、未対応タグ / プロパティ / 値、フォント読込失敗、上限超過（`LimitExceeded`）、メモリ不足（`OutOfMemory`）。**どの入力のどこが原因かを必ず含める**
- 信頼できない HTML を受けるときは `RenderOptions::limits` で予算を決める。メモリ不足の扱い（`OutOfMemory` は最善努力で、保証は上限の側）は ARCHITECTURE.md A26
- **圧縮レベルも「入力の一部」**。同じ HTML + 同じ `RenderOptions` なら常に同じバイト列が出る。レベルを変えるとファイルの大きさは変わるが、デコードした画素は 1 ビットも変わらない（ARCHITECTURE.md A33）
- **連続生成**（OG 画像をリクエストごとに作る、という本来の用途）では `LoadedFonts` / `LoadedImages` を使う。効くのは時間より**メモリと並行度**で、8 並行で 96 本組んだときの RSS が 194 → 110 MiB。画像を使い回す効果はもっと大きい（全面背景のデコードは 1 回あたり 16 ms = `render()` の 26%）。詳細と計測は ARCHITECTURE.md A34
- CLI も薄く用意する: `shashoku input.html --font NotoSansJP.ttf -o out.png --dump-stage=box`

## 9. 開発フェーズ（出口から通す）

各フェーズに受け入れ条件を置く。**Phase 4 が終わった時点で製品の核が完成**していることに注意（HTML すら Phase 5 まで登場しない）。

- **Phase 0: PNG エンコーダ**
  `Bitmap`（手作りの単色 / グラデ画像）→ 正当な PNG ファイル。自作チャンク書き出し＋フィルタ＋zlib
  ✅ 生成した PNG が画像ビューアと `pngcheck` で開ける
- **Phase 1: ラスタライザ最小**
  `DisplayList`（手書きで構築）→ `Bitmap`。FillRect と source-over 合成
  ✅ 重なった半透明矩形のゴールデンテストが通る
- **Phase 2: 文字を 1 行描く**
  FreeType + HarfBuzz 導入。固定文字列 → ShapedRun → DrawGlyphs → ベースラインに沿った描画。FontStore とフォールバック
  ✅ 「こんにちは、世界のみんな。ABC😀」が 1 行で正しく描ける（絵文字は豆腐警告で可）
- **Phase 3: block レイアウト＋素朴な行分割**
  手書きのスタイル付きツリー → ボックスツリー。幅の下降・高さの上昇、margin/padding、素朴な文字数折り返し
  ✅ 幅 200px に 13 文字を流すと 2 行になり、高さが自動で決まる
- **Phase 4: 禁則処理 ★ここが本体**
  LineBreaker を UAX #14 サブセット＋ JIS X 4051 禁則テーブルで実装。追い出し / 追い込み / ぶら下げ
  ✅ テーブル駆動テスト（禁則ケース網羅）が全通過。行頭に句読点が絶対に出ない
- **Phase 5: HTML / CSS フロントエンド接続**
  自作 HTML パーサ＋ style 属性のスタイル解決（カスケード・継承・計算値化）。ここで初めて `render()` が end-to-end で繋がる
  ✅ README のサンプル HTML が 1 コールで PNG になる
- **Phase 6: flexbox**
  ✅ OG 画像の典型レイアウト（アイコン＋タイトル＋フッター）が組める
- **Phase 7: ルビ**
- **Phase 8: 縦書き**
- **Phase 9: 配布**（CLI 整備、ドキュメント、必要なら Node バインディング検討）
  - **9a: 試用版（#20）** — 「初めて触る人が準備なしで 1 枚作れる」までを先に出す。
    linux-x86_64 / glibc 2.35 以降で単体で動く実行ファイル（C++ ランタイムだけ静的リンク。
    完全静的は glibc の LGPL を理由に却下した。A39）、CLI に埋め込む既定フォント
    （Noto Sans JP Regular / Bold。**CLI 層だけの機能**で `render()` の署名は変えない。A38）、
    動くサンプル 5 本、`v*` タグで**ドラフトの**リリースを作る workflow（公開はユーザーが
    GitHub 上で "Publish release" を押して行う）、試用報告の受け口
    ✅ 展開して `./shashoku examples/og_card.html --image icon=examples/icon.png -o og.png` が
    素の Ubuntu 22.04 / 24.04 で通り、既定フォントの PNG が `--font <同じ OTF>` とバイト単位で一致する
  - **9b: C++ の配布 API** — `install()` / `find_package(shashoku)` / パッケージ設定ファイル。未着手
  - **9c: その他** — deb・Homebrew・Docker、Node バインディング、macOS / Windows / aarch64 の成果物。
    aarch64 はゴールデン画像を別に持つ覚悟が要るので、先に決めること（#20 の「決めること」1 番）

## 10. テスト戦略

1. **ゴールデンテスト**: 入力（HTML + フォント + オプション）→ 期待 PNG のピクセル完全一致。純粋関数なので成立する。差分時は actual / expected / diff の 3 枚を吐く
2. **行分割器のテーブル駆動テスト**: `{入力文字列, 幅, ポリシー} → 期待される行分割` を大量に列挙。禁則の全文字クラスをカバー。**このテスト群が実質的な仕様書になる**
3. **段階ダンプの構造テスト**: ボックスツリーを JSON ダンプして座標を検証（ピクセルより先にここで壊れを検出）
4. **ファジング**: ランダムな日本語文字列（絵文字・結合文字・サロゲート含む）を流し、クラッシュしない＋禁則違反が出ないことを検査
5. **固定フォントをテストアセットに同梱**（Noto Sans JP のサブセット等、ライセンス確認の上）。フォントが変わるとゴールデンが崩れるため

## 11. 開発の進め方（Claude Code への期待役割）

> **2026-09-19 方針変更**: 以下の「コアは人間が書く」は失効。実装は Claude のサブエージェントが行い、
> メインの Claude が設計とオーケストレーションを担当する。現行の役割分担は CLAUDE.md、
> 実装レベルの設計は [ARCHITECTURE.md](ARCHITECTURE.md) を参照。以下は当初の方針の記録として残す。

このプロジェクトは学習目的を兼ねる。**コア（レイアウトエンジン・行分割器・PNG エンコーダ・ラスタライズ）は人間が自分の手で書く。** Claude Code に頼みたいのは：

- 設計の壁打ち・レビュー（この設計書との整合性チェック含む）
- CMake / CI / clang-format 等のプロジェクト基盤整備
- テストケースの列挙と整備（特に禁則のテーブル駆動テストの網羅）
- FreeType / HarfBuzz の API 調査と最小サンプル
- 仕様の調査補助（UAX #14、JIS X 4051、WHATWG、PNG 仕様の該当箇所の要約）

コア部分の実装コードを先回りして書かないこと。求められたら擬似コードとレビューで支援する。

---

## 付録: 用語の対応（この設計書の背骨）

- HTML → PNG とは「**木を作り変えながら（DOM → スタイル付き → ボックス）、描画命令に落として塗る**」作業
- 文字コード＝フォント非依存のキー、cmap＝フォントごとの辞書、グリフ ID＝フォント内ローカルなポインタ
- SVG / PDF＝ディスプレイリストのシリアライズ形式（計算済み・未実行）、PNG / JPG＝実行結果の保存形式
- 禁則処理の住所は「段階③の行分割器の中」。shashoku の存在理由はそこにある
