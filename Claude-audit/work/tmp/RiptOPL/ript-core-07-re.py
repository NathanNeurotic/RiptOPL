import os, re, sys, collections
# usage: root subdirs(comma) pattern... ; prints count and files per regex
root = sys.argv[1]
subs = sys.argv[2].split(',')
pats = sys.argv[3:]
rx = [re.compile(p) for p in pats]
cnt = collections.Counter()
files = collections.defaultdict(set)
for s in subs:
    for dp, dn, fn in os.walk(os.path.join(root, s)):
        for f in fn:
            p = os.path.join(dp, f)
            try:
                if os.path.getsize(p) > 3000000:
                    continue
                t = open(p, 'rb').read().decode('utf-8', 'replace')
            except Exception:
                continue
            rel = os.path.relpath(p, root).replace('\\', '/')
            for i, r in enumerate(rx):
                for m in r.finditer(t):
                    cnt[i] += 1
                    files[i].add(rel + ':' + str(t.count('\n', 0, m.start()) + 1))
for i, p in enumerate(pats):
    fl = sorted(files[i])
    print(p, cnt[i], ' '.join(fl[:5]))
