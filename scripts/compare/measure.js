// Chrome（headless）のページ内で走らせる測定コード。
//
// 依存ゼロで動かすため、CDP（WebSocket）のクライアントは使わない。代わりにこのコードが
// ページ内で測り、結果を JSON → UTF-8 → base64 にして、id が shk-result の div の
// data-json 属性に書き出す。compare.py は
// `chrome.exe --headless --dump-dom` の標準出力からこれを取り出す。
// base64 にするのは、DOM の直列化で `<` や `&` がエスケープされるのを避けるため。
//
// 座標は「論理座標」に直してから返す（shashoku の --dump-stage box と同じ向き）。
//   横書き: inline = x（左→右）、block = y（上→下）
//   縦書き (vertical-rl): inline = y（上→下）、block = 右→左なので -x を使う
// block の原点は shashoku と違うが、block 方向は各側の中だけで比べるので問題にならない。

'use strict';

window.__shkMeasure = function (opts) {
  var vertical = !!opts.vertical;
  var round = function (v) { return Math.round(v * 1000) / 1000; };

  var ax = vertical
    ? {
        inline_start: function (r) { return r.top; },
        inline_end: function (r) { return r.bottom; },
        block_start: function (r) { return -r.right; },
        block_end: function (r) { return -r.left; }
      }
    : {
        inline_start: function (r) { return r.left; },
        inline_end: function (r) { return r.right; },
        block_start: function (r) { return r.top; },
        block_end: function (r) { return r.bottom; }
      };

  function box(r) {
    return {
      inline_start: round(ax.inline_start(r)),
      inline_end: round(ax.inline_end(r)),
      block_start: round(ax.block_start(r)),
      block_end: round(ax.block_end(r))
    };
  }

  // 「紙面」は window.innerWidth ではなく、compare.py が渡す大きさで決める。
  // WSL から呼ぶ chrome.exe では --window-size より小さいウィンドウを作れず（実測: 幅は
  // 500 px 未満にならない）、--dump-dom のときは window.innerWidth が 0 になることもある。
  // 高さ（縦書きなら幅）を指定しないケースでは、shashoku と同じく「ルートの広がり」を紙面にする。
  var rootEl = document.getElementById('shk-root');
  var rootBox = null;
  function viewportBox() {
    var w = opts.width, h = opts.height;
    if (!h) {
      rootBox = rootBox || rootEl.getBoundingClientRect();
      h = rootBox.height;
    }
    return box({ left: 0, top: 0, right: w, bottom: h, width: w, height: h });
  }
  var viewport = viewportBox();

  var segmenter = new Intl.Segmenter(undefined, { granularity: 'grapheme' });
  var warnings = [];

  function display(el) { return window.getComputedStyle(el).display; }

  function isBlockLevel(el) {
    var d = display(el);
    return d === 'block' || d === 'flex' || d === 'grid' || d === 'list-item' ||
           d === 'flow-root' || d === 'table';
  }

  // shashoku の box ダンプで `lines` を持つノード（= 行ボックスの親）に当たるものを集める。
  function collectBlocks(el, out) {
    var kids = [];
    for (var i = 0; i < el.children.length; i++) {
      if (display(el.children[i]) !== 'none') kids.push(el.children[i]);
    }
    var blockKids = kids.filter(isBlockLevel);
    if (blockKids.length === 0) {
      out.push(el);
      return;
    }
    if (blockKids.length !== kids.length) {
      warnings.push('block と inline が混ざった要素がある: ' + el.tagName.toLowerCase());
    }
    blockKids.forEach(function (c) { collectBlocks(c, out); });
  }

  // 1 クラスタ = 1 書記素。ルビ（rt の中）は ruby: true を立てて、行の組み立てからは外す。
  function collectItems(block) {
    var items = [];

    function pushText(node, inRuby) {
      var data = node.data;
      if (data.length === 0) return;
      for (const seg of segmenter.segment(data)) {
        var range = document.createRange();
        range.setStart(node, seg.index);
        range.setEnd(node, seg.index + seg.segment.length);
        var r = range.getBoundingClientRect();
        if (r.width === 0 && r.height === 0) continue;  // 畳まれた空白
        var b = box(r);
        b.kind = 'text';
        b.text = seg.segment;
        b.ruby = inRuby;
        items.push(b);
      }
    }

    function pushImage(el, inRuby) {
      var b = box(el.getBoundingClientRect());
      b.kind = 'image';
      b.text = '';
      b.ruby = inRuby;
      items.push(b);
    }

    // flex アイテムの <img> のように、ブロックそのものが画像のことがある。
    if (block.tagName.toLowerCase() === 'img') {
      pushImage(block, false);
      return items;
    }

    (function visit(node, inRuby) {
      for (var c = node.firstChild; c; c = c.nextSibling) {
        if (c.nodeType === Node.TEXT_NODE) {
          pushText(c, inRuby);
        } else if (c.nodeType === Node.ELEMENT_NODE) {
          if (display(c) === 'none') continue;
          var tag = c.tagName.toLowerCase();
          if (tag === 'img') { pushImage(c, inRuby); continue; }
          visit(c, inRuby || tag === 'rt');
        }
      }
    })(block, false);

    return items;
  }

  // ルビ組は DOM から直接取る（座標から組を推測しない）。
  // base は `<ruby>` の中で `<rt>` / `<rp>` に入っていないテキスト、rt は `<rt>` のテキスト。
  function rubyGroups(block) {
    var groups = [];
    var rubies = block.tagName.toLowerCase() === 'ruby'
      ? [block] : Array.prototype.slice.call(block.querySelectorAll('ruby'));
    rubies.forEach(function (ruby) {
      var base = [], annotation = [];
      (function visit(node, inRt) {
        for (var c = node.firstChild; c; c = c.nextSibling) {
          if (c.nodeType === Node.TEXT_NODE) {
            if (c.data.trim() === '') continue;
            var range = document.createRange();
            range.selectNodeContents(c);
            (inRt ? annotation : base).push(box(range.getBoundingClientRect()));
            if (!inRt) {
              // 末尾クラスタの開始も取る（送りの中央かインクの中央かを読み取るため）
              var last = document.createRange();
              var segs = Array.from(segmenter.segment(c.data));
              var s = segs[segs.length - 1];
              last.setStart(c, s.index);
              last.setEnd(c, s.index + s.segment.length);
              base[base.length - 1].last_cluster_start = box(
                last.getBoundingClientRect()).inline_start;
            }
          } else if (c.nodeType === Node.ELEMENT_NODE) {
            var tag = c.tagName.toLowerCase();
            if (tag === 'rp' || display(c) === 'none') continue;
            visit(c, inRt || tag === 'rt');
          }
        }
      })(ruby, false);
      if (base.length === 0 || annotation.length === 0) return;
      groups.push({
        base_advance: [Math.min.apply(null, base.map(function (b) { return b.inline_start; })),
                       Math.max.apply(null, base.map(function (b) { return b.inline_end; }))],
        base_last_cluster_start: base[base.length - 1].last_cluster_start,
        rt_box: [Math.min.apply(null, annotation.map(function (b) { return b.inline_start; })),
                 Math.max.apply(null, annotation.map(function (b) { return b.inline_end; }))],
        text: ruby.textContent.replace(/\s+/g, '')
      });
    });
    return groups;
  }

  // 行の切れ目は「inline 座標が戻り、かつ block 座標が進んだところ」で見る。
  // inline が戻っただけで切ると、ルビ組で親文字の箱が注記の幅まで広がったときに
  // 誤検出する（実測: 縦書きのケース 2 で、親文字の箱の終端 160 のあとに続きの文字が 152 から始まる）。
  function groupLines(items) {
    var lines = [];
    var cur = null;
    items.forEach(function (it) {
      if (it.ruby) return;
      if (cur === null ||
          (it.inline_start < cur.inline_end - 0.5 && it.block_start > cur.block_start + 0.5)) {
        cur = {
          inline_start: it.inline_start, inline_end: it.inline_end,
          block_start: it.block_start, block_end: it.block_end,
          items: [], ruby: []
        };
        lines.push(cur);
      }
      cur.items.push(it);
      cur.inline_start = Math.min(cur.inline_start, it.inline_start);
      cur.inline_end = Math.max(cur.inline_end, it.inline_end);
      cur.block_start = Math.min(cur.block_start, it.block_start);
      cur.block_end = Math.max(cur.block_end, it.block_end);
    });

    // ルビは inline 範囲が最も重なる行に入れる（行の組み立てには使わない）。
    items.forEach(function (it) {
      if (!it.ruby) return;
      var best = null, bestOverlap = -1;
      lines.forEach(function (ln) {
        var o = Math.min(ln.inline_end, it.inline_end) - Math.max(ln.inline_start, it.inline_start);
        if (o > bestOverlap) { bestOverlap = o; best = ln; }
      });
      if (best !== null) best.ruby.push(it);
    });
    return lines;
  }

  var root = document.getElementById('shk-root');
  var blocks = [];
  collectBlocks(root, blocks);

  var result = {
    viewport: viewport,
    vertical: vertical,
    device_pixel_ratio: window.devicePixelRatio,
    window_inner_width: window.innerWidth,
    window_inner_height: window.innerHeight,
    root_rect: box(root.getBoundingClientRect()),
    blocks: blocks.map(function (el) {
      var items = collectItems(el);
      // 祖先の箱との共通部分。flex アイテムのように自分の箱が親より広くなることがあるので、
      // はみ出しは祖先まで見ないと測れない。
      var clip = box(el.getBoundingClientRect());
      for (var a = el; a && a !== document.body; a = a.parentElement) {
        var r = box(a.getBoundingClientRect());
        clip = {
          inline_start: Math.max(clip.inline_start, r.inline_start),
          inline_end: Math.min(clip.inline_end, r.inline_end),
          block_start: Math.max(clip.block_start, r.block_start),
          block_end: Math.min(clip.block_end, r.block_end)
        };
      }
      return {
        tag: el.tagName.toLowerCase(),
        rect: box(el.getBoundingClientRect()),
        clip: clip,
        lines: groupLines(items),
        ruby_groups: rubyGroups(el)
      };
    }),
    fonts: Array.from(document.fonts).map(function (f) {
      return { family: f.family, weight: f.weight, status: f.status };
    }),
    fonts_status: document.fonts.status,
    warnings: warnings
  };

  // 測定が終わってから書き出す（このノード自体がレイアウトに影響しないよう display:none）。
  var out = document.createElement('div');
  out.id = 'shk-result';
  out.style.display = 'none';
  var bytes = new TextEncoder().encode(JSON.stringify(result));
  var bin = '';
  for (var i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i]);
  out.setAttribute('data-json', btoa(bin));
  document.body.appendChild(out);
};
