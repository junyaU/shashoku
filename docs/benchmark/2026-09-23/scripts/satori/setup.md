# Satori + resvg の導入記録（2026-09-23）

## 環境

| | |
|---|---|
| CPU | Intel(R) Core(TM) i9-14900KF（32 論理コア） |
| OS | Linux 5.15.167.4-microsoft-standard-WSL2 |
| システムの Node | v12.22.9（Satori は Node >= 16 が必要なので使えない） |
| 使った Node | v20.18.1（linux-x64 公式バイナリを `EXP/satori/node/` に展開。システムには入れていない） |
| satori | 0.33.5 |
| satori-html | 0.3.2 |
| @resvg/resvg-js | 2.6.2 |

## 手順（6 手、機械時間の合計は約 13 秒）

```
1. curl -L -o node.tar.xz https://nodejs.org/dist/v20.18.1/node-v20.18.1-linux-x64.tar.xz   # 25.8 MB / 1.2 秒
2. tar -xJf node.tar.xz                                                                     # 1.1 秒
3. export PATH=EXP/satori/node/node-v20.18.1-linux-x64/bin:$PATH
4. npm init -y                                                                              # 3.7 秒
5. npm install satori satori-html @resvg/resvg-js                                           # 4.0 秒、27 パッケージ
6. render.mjs を書く（約 50 行）
```

## ダウンロード量と大きさ

- Node 20 のバイナリ: 25.8 MB（圧縮）/ 展開後 約 90 MB
- `node_modules`: **20 MB / 374 ファイル / 27 パッケージ**
  - 内訳: satori 5.7 MB、@resvg 4.3 MB（ネイティブ .node）、harfbuzzjs 3.2 MB（wasm）、@shuding 3.0 MB（opentype）、
    pako 796 KB、fflate 560 KB、yoga-layout 324 KB（wasm）、ultrahtml 244 KB、linebreak 236 KB
- 合計して、何もない機械で動かすまでに **約 116 MB** を落とす

## つまずいた点

1. **システムの Node が v12 で使えない。** Satori は Node >= 16。sudo が使えない前提だったので公式バイナリを自前で展開した。
   npm install 自体は一度も失敗しなかった。
2. **`@resvg/resvg-js` はネイティブ addon。** linux-x64 の prebuilt が降ってきたので問題なかったが、
   別 arch / musl では話が変わる。「Node だけあればいい」ではない。
3. **フォント: OTF/CFF はそのまま読めた。** `NotoSansJP-Regular.otf` / `-Bold.otf` を `fonts` にそのまま渡して描画できたので、
   TTF を別途取得する必要はなかった。`EXP/satori/fonts/` は作っていない。使ったのは `EXP/bin/fonts/` の 3 本
   （NotoSansJP-Regular.otf=400, NotoSansJP-Bold.otf=700, NotoSans-Regular.ttf=400）。
4. **`<img width="56" height="56">` が効かない。** satori-html は属性を文字列で渡すため Satori が
   `Invalid value "56" for "width"` と標準エラーに出し、**画像を描かずに exit 0 で終わる**。
   `style="width:56px;height:56px"` に変える必要があった（case06）。
5. **Satori は「高さ」が必須。** 内容に合わせて伸ばすことができないので、全ケースで高さを目視で決め直した。

## render.mjs

`node render.mjs <input.html> <width> <height> <output.png>`。
satori-html で HTML 文字列を Satori の入力に変換 → `satori()` で SVG → `Resvg` で PNG。
デバッグ用に中間の SVG も同じ名前で書き出す。
