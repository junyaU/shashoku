import json,sys,collections
p=sys.argv[1]
d=json.load(open(p))
print('ok=',d['ok'],'w=',d.get('width'),'h=',d.get('height'),'truncated=',d.get('truncated'))
for key in ('errors','warnings'):
    lst=d.get(key) or []
    c=collections.Counter(e.get('kind') for e in lst)
    print(f'{key}: {len(lst)} {dict(c)}')
