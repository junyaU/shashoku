# shashoku（写植）— 日本語組版特化 HTML→PNG レンダリングエンジン 設計書

> Claude Code に渡す前提のプロジェクト定義書。設計思想・スコープ・アーキテクチャ・フェーズ計画のすべてをここに集約する。
> 名前の由来: 写真植字機（写植）— 日本語の活字を組み、画像として出力していた機械。本プロジェクトはその再発明にあたる。

---

## 0. 一文ミッション

**日本語の文章を絶対に破綻させずに、HTML から PNG を一発で生成する組版エンジン。**

Satori が「flexbox で作ったカードを SVG にする汎用エンジン」だとすれば、shashoku は「日本語の組版品質を保証して PNG まで出し切る特化エンジン」。想定ユーザーは「Satori を知らない人」ではなく **「Satori で日本語が崩れた経験のある人」**。

## 1. 背景 / 解決する課題

サーバーサイドでの HTML→画像変換（OG 画像生成が最大の用途）には現在 2 つの選択肢しかない：

1. **headless Chrome (Playwright/Puppeteer)** — 何でも描けるが、数百 MB のバイナリと依存地獄。リクエスト毎の生成には重すぎる
2. **Satori (+resvg)** — 軽量だが、日本語組版が弱い：
   - **禁則処理がない**: 動的に流し込んだタイトルで行頭に「。」「、」が落ちる。組版として明確に壊れている
   - **縦書き非対応**: `writing-mode: vertical-rl` が存在しない。小説・短歌・書籍系サービスは門前払い
   - **ルビ非対応**: `<ruby>` が使えない
   - **フォント運用が毎回手作り**: フォールバック連鎖・サブセット・豆腐対策をユーザーが配管する
   - **SVG→PNG の 2 段構成**: satori + resvg-js + フォント二重管理の配管工事、障害時の切り分けコスト

OG 画像は動的なタイトル文字列を流し込むものなので、**任意の日本語文字列で組版が破綻しないことが本質要件**。ここを保証するエンジンは存在しない。shashoku はこの穴を埋める。

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

### 機能追加の判定基準（迷ったらこの一問）

**「それは日本語の文章を正しく組むことに寄与するか？」**
- Yes → 入れる（例: `text-orientation`, `line-break`, ルビ）
- No → 削る / 後回し（例: `box-shadow`, アニメーション, grid）

## 3. 設計原則

1. **一方向パイプライン**: 意味 → 座標 → ピクセル への片道変換。各段は前段の出力だけを入力とし、後戻りしない。段が終われば前段の中間表現は破棄してよい（静的一発変換であり、ブラウザのような差分再計算はしない）
2. **重いデータは参照で引き回す**: グリフ輪郭・フォント実体は FontStore が所有し、ツリーには ID（FontId, glyph_id）だけを載せる。デリファレンスはラスタライズの瞬間のみ
3. **各段の出力はダンプ可能**: `--dump-stage=dom|style|box|display-list|svg` で全中間表現を目視できる。デバッグとテストの基盤
4. **行分割器は独立モジュール**: 製品のコアなので、レイアウトエンジンから分離してテーブル駆動で単体テストできる形にする。テキスト計測器（TextMeasurer）はインターフェースとしてレイアウトに注入する（レイアウト⇄計測の相互再帰を疎結合に保つ）
5. **純粋関数**: 同じ入力（HTML + フォント + オプション）からは常にバイト単位で同じ PNG が出る。グローバル状態・時刻・乱数・ネットワークへの依存なし
6. **fail loudly**: 未対応のタグ・プロパティ・値は警告ではなくエラー。ただし豆腐（グリフ欠落）は警告リストとして返し、描画は続行する（代替グリフ □ を描く）

## 4. スコープ

### 対応する入力

- HTML サブセット: `div`, `span`, `p`, `h1`-`h6`, `img`, `ruby`, `rt`, `br`, テキストノード
- スタイル指定: `style` 属性（Phase 5 で `<style>` + 単純セレクタ tag / .class / #id を追加）
- 文字コード: UTF-8 のみ

### 対応 CSS プロパティ（初期セット）

```
display (block | flex | inline | none)
width, height, margin*, padding*
font-size, font-family, font-weight, line-height, color
background-color, border, border-radius
text-align, letter-spacing
flex-direction, justify-content, align-items, gap, flex-grow/shrink/basis
line-break (auto | strict | loose), overflow-wrap
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
6. **豆腐検出**: どのフォントにもグリフがないコードポイントは警告リスト（コードポイント＋位置）として `RenderResult` に積み、□ を描画して続行する

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
};

struct Warning { WarningKind kind; std::string detail; };

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

}  // namespace shashoku
```

- エラー（`RenderError`）: パース失敗、未対応タグ / プロパティ / 値、フォント読込失敗。**どの入力のどこが原因かを必ず含める**
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
