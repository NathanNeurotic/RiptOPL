import os, re, sys, collections
root = sys.argv[1]
names = sys.argv[2:]
want = set(names)
cnt = collections.Counter()
files = collections.defaultdict(set)
tok = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')
for dp, dn, fn in os.walk(root):
    rel = os.path.relpath(dp, root).replace('\\', '/')
    if rel.startswith('.git') or rel.startswith('docs') or rel.startswith('ship') or rel.startswith('third_party/mbedtls'):
        dn[:] = []
        continue
    for f in fn:
        p = os.path.join(dp, f)
        try:
            if os.path.getsize(p) > 3000000:
                continue
            data = open(p, 'rb').read()
        except Exception:
            continue
        if b'\0' in data[:4096]:
            continue
        t = data.decode('utf-8', 'replace')
        for m in tok.findall(t):
            if m in want:
                cnt[m] += 1
                files[m].add((rel + '/' + f).lstrip('./'))
for n in names:
    fl = sorted(files[n])
    print(n, cnt[n], len(fl), ' '.join(fl[:6]))
