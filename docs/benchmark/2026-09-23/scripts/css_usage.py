#!/usr/bin/env python3
"""HTML ファイル群が使うタグ・CSS プロパティを数え、shashoku の対応表と突き合わせる（標準ライブラリのみ）。
使い方: css_usage.py <out.json> <in1.html> [in2.html ...]
"""
import sys, re, json, collections
from html.parser import HTMLParser

SUPPORTED_TAGS = {"br","div","h1","h2","h3","h4","h5","h6","img","p","rp","rt","ruby","span","style"}
SUPPORTED_PROPS = {
 "background","background-color","border","border-color","border-radius","border-style","border-width",
 "color","column-gap","display","flex","flex-basis","flex-direction","flex-grow","flex-shrink",
 "font-family","font-size","font-weight","gap","height","justify-content","letter-spacing","line-break",
 "line-height","margin","margin-bottom","margin-left","margin-right","margin-top","overflow-wrap","padding",
 "padding-bottom","padding-left","padding-right","padding-top","row-gap","text-align","width","word-wrap",
 "writing-mode","align-items",
}
# 対応プロパティでも値が対応外になりうるもの（粗い検査。単位 % は width/flex-basis のみ、rem/vw/vh/pt/ch は不可、calc/var/gradient 不可）
BAD_VALUE_RE = re.compile(r"\b(rem|vw|vh|vmin|vmax|pt|ch|ex)\b|calc\(|var\(|gradient\(|!important|\bauto\b(?!.*margin)")

class P(HTMLParser):
    def __init__(self):
        super().__init__(); self.tags = collections.Counter(); self.styles = []; self.inline = []; self._in_style = False
        self.attrs = collections.Counter()
    def handle_starttag(self, tag, attrs):
        self.tags[tag] += 1
        for k, v in attrs:
            self.attrs[f"{tag}@{k}"] += 1
            if k == "style" and v: self.inline.append(v)
        if tag == "style": self._in_style = True
    def handle_endtag(self, tag):
        if tag == "style": self._in_style = False
    def handle_data(self, data):
        if self._in_style: self.styles.append(data)

def decls_from_css(css):
    css = re.sub(r"/\*.*?\*/", "", css, flags=re.S)
    out = []
    # @rules (media, font-face, keyframes, import)
    at = re.findall(r"@([a-zA-Z-]+)", css)
    for m in re.finditer(r"\{([^{}]*)\}", css):
        for d in m.group(1).split(";"):
            if ":" in d:
                k, v = d.split(":", 1); out.append((k.strip().lower(), v.strip()))
    return out, at

def selectors_from_css(css):
    css = re.sub(r"/\*.*?\*/", "", css, flags=re.S)
    sels = re.findall(r"([^{}]+)\{", css)
    kinds = collections.Counter()
    for s in sels:
        s = s.strip()
        if s.startswith("@"): kinds["@rule"] += 1; continue
        for part in s.split(","):
            part = part.strip()
            if re.search(r"::?[a-z-]+", part): kinds["pseudo"] += 1
            elif re.search(r"[\s>+~]", part): kinds["combinator"] += 1
            elif re.search(r"\[", part): kinds["attribute"] += 1
            elif part == "*": kinds["universal"] += 1
            elif re.fullmatch(r"[a-zA-Z0-9]*(\.[\w-]+|#[\w-]+)*", part): kinds["simple"] += 1
            else: kinds["other"] += 1
    return kinds

result = {"files": {}, "summary": {}}
prop_files = collections.Counter(); tag_files = collections.Counter(); sel_files = collections.Counter(); at_files = collections.Counter()
val_files = collections.Counter()
for path in sys.argv[2:]:
    src = open(path, encoding="utf-8").read()
    p = P(); p.feed(src)
    decls, ats = [], []
    for css in p.styles:
        d, a = decls_from_css(css); decls += d; ats += a
    for st in p.inline:
        d, _ = decls_from_css("{" + st + "}"); decls += d
    props = collections.Counter(k for k, _ in decls)
    unsupported_props = sorted(k for k in props if k not in SUPPORTED_PROPS and not k.startswith("--"))
    unsupported_tags = sorted(t for t in p.tags if t not in SUPPORTED_TAGS and t not in ("html","head","body","meta","title","link"))
    wrapper_tags = sorted(t for t in p.tags if t in ("html","head","body","meta","title","link"))
    bad_values = sorted({f"{k}: {v}" for k, v in decls if k in SUPPORTED_PROPS and BAD_VALUE_RE.search(v)})
    sels = selectors_from_css("".join(p.styles))
    result["files"][path.split("/")[-1]] = {
        "tags": dict(p.tags), "unsupported_tags": unsupported_tags, "wrapper_tags": wrapper_tags,
        "props": dict(props), "unsupported_props": unsupported_props, "suspicious_values": bad_values,
        "selectors": dict(sels), "at_rules": sorted(set(ats)), "emoji": bool(re.search(r"[\U0001F300-\U0001FAFF☀-➿]", src)),
    }
    for k in set(props): prop_files[k] += 1
    for t in set(p.tags): tag_files[t] += 1
    for k in sels: sel_files[k] += 1
    for a in set(ats): at_files[a] += 1
    for bv in bad_values: val_files[bv.split(":")[0]] += 1
n = len(sys.argv) - 2
result["summary"] = {
    "files": n,
    "unsupported_props_by_file_count": {k: c for k, c in prop_files.most_common() if k not in SUPPORTED_PROPS and not k.startswith("--")},
    "unsupported_tags_by_file_count": {k: c for k, c in tag_files.most_common() if k not in SUPPORTED_TAGS},
    "selector_kinds_by_file_count": dict(sel_files.most_common()),
    "at_rules_by_file_count": dict(at_files.most_common()),
    "supported_props_with_suspicious_values_by_file_count": dict(val_files.most_common()),
    "files_with_emoji": sum(1 for f in result["files"].values() if f["emoji"]),
}
json.dump(result, open(sys.argv[1], "w"), ensure_ascii=False, indent=1)
s = result["summary"]
print(f"files={n}")
print("unsupported props (files using):", s["unsupported_props_by_file_count"])
print("unsupported tags (files using):", s["unsupported_tags_by_file_count"])
print("selectors:", s["selector_kinds_by_file_count"], "at-rules:", s["at_rules_by_file_count"])
print("suspicious values in supported props:", s["supported_props_with_suspicious_values_by_file_count"], "emoji files:", s["files_with_emoji"])
