import json,sys,collections
d=json.load(open(sys.argv[1]))
print("ok=%s width=%s height=%s truncated=%s errors=%d warnings=%d"%(d["ok"],d["width"],d["height"],d["truncated"],len(d["errors"]),len(d["warnings"])))
for label in ("errors","warnings"):
    if not d[label]: continue
    print(label,"kinds:",dict(collections.Counter(e.get("kind") for e in d[label])))
    for e in d[label]:
        print("  [%s] %d:%d %s%s"%(e.get("kind"),e.get("line") or 0,e.get("column") or 0,(e.get("message") or e.get("detail")),("\n      HINT: "+e["hint"]) if e.get("hint") else ""))
