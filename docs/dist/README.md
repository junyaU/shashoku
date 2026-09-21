# shashoku（写植）— 試用版

**日本語の文章を絶対に破綻させずに、HTML から PNG を一発で生成する組版エンジン。**

実行ファイルは 1 つだけで、依存ライブラリも**既定フォント**（Noto Sans JP Regular / Bold）も
中に入っています。展開したその場で動きます（ビルドもフォントの用意も要りません）。

**対応環境: linux-x86_64 / glibc 2.35 以降**（Ubuntu 22.04 以降、Debian 12 以降など）。
C++ ランタイム（libc++ など）は静的リンク済みで、動的に要るのは libc と libm だけです
（`ldd shashoku` で確かめられます）。musl の環境（Alpine など）では動きません。

```bash
./shashoku examples/og_card.html --image icon=examples/icon.png -o og.png --width 1200 --height 630
```

版は `./shashoku --version`、ライセンスは `./shashoku --license` で出ます。

## 入っているもの

| | |
|---|---|
| `shashoku` | 実行ファイル（linux-x86_64。C++ ランタイムは静的リンク済み） |
| `examples/` | サンプル 5 本。先頭コメントに**そのまま貼れる 1 行**が書いてあります |
| `LICENSE` | shashoku 本体（MIT License） |
| `THIRD_PARTY_LICENSES` | zlib / FreeType / HarfBuzz / Noto Sans JP（SIL OFL 1.1） |

## サンプル

どれも `--font` を書かずに動きます。

```bash
./shashoku examples/hello.html -o hello.png --width 600
./shashoku examples/og_card.html --image icon=examples/icon.png -o og_card.png --width 1200 --height 630
./shashoku examples/ruby.html -o ruby.png --width 460
./shashoku examples/vertical.html -o vertical.png --width 520 --height 560
./shashoku examples/kinsoku.html -o kinsoku.png --width 520
```

| ファイル | 何を見るか |
|---|---|
| `hello.html` | 基本。見出しと本文、禁則処理 |
| `og_card.html` | OG 画像の典型。flexbox、画像、太字（既定フォントの Bold） |
| `ruby.html` | ルビ（`<ruby>` / `<rt>` / `<rp>`）。モノルビも |
| `vertical.html` | 縦書き（`writing-mode: vertical-rl`）。約物の字形差し替え、欧文の横倒し |
| `kinsoku.html` | 禁則処理と、あふれ処理 3 方式（`--overflow oidashi \| oikomi \| burasage`） |

## 覚えておくとよいこと

- 入力は **HTML の断片**です（`<html>` や `<body>` を書くとエラーになります）。文字コードは UTF-8 のみ
- 対応していないタグ・プロパティ・値は、**黙って無視せずエラー**になります（原因の位置つき）
- `box-sizing` は content-box だけ。`width` / `height` は padding と border を**含まない**値です
- 画像は PNG のみ。URL もファイルパスも解釈しないので `--image <名前>=<パス>` で渡します
- 縦書きでは内容が横に伸びるので `--height` が要ります
- `--font A.otf --font B.otf` で自分のフォントを使えます（**指定順がフォールバック順**）
- 同じ入力からは常にバイト単位で同じ PNG が出ます（純粋関数。時刻も乱数もネットワークも使いません）
- 依存ライブラリと既定フォントの版で絵は変わります。`./shashoku --version` で全部出ます

すべてのオプションは `./shashoku --help` で出ます。

## うまくいかなかったら教えてください

試用で集めた「引っかかったところ」が、次に対応する CSS を決めます。

https://github.com/junyaU/shashoku/issues/new/choose の「試用報告」から、
`./shashoku --version` の出力・入力した HTML の全文・実行したコマンド行・
エラーの全文・期待した絵を貼ってください。

- リポジトリ: https://github.com/junyaU/shashoku
- 設計: `docs/DESIGN.md` / `docs/ARCHITECTURE.md`（リポジトリにあります）

---

This software is based in part on the work of the FreeType Team.
