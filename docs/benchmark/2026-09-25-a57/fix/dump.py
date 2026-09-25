import json,sys
d=json.load(open(sys.argv[1]))
for e in d.get('errors') or []:
    h=e.get('hint') or ''
    print(f"{e['line']}:{e['column']} [{e['kind']}] {e['message']}" + (f"\n    HINT: {h}" if h else ""))
for w in d.get('warnings') or []:
    print(f"{w.get('line')}:{w.get('column')} [warn {w['kind']}] {w.get('detail')} overflow_px={w.get('overflow_px')} edge={w.get('edge')}")
