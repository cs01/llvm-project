# Find the expat storeRawNames shape mechanically: realloc's argument pointer
# read again after the call, before it is reassigned. Text-level and therefore
# noisy; it is a triage filter, not an oracle.
import re, sys, os

# Case-insensitive: projects wrap realloc in an uppercase macro (expat's
# REALLOC, redis's s_realloc_usable), and the wrapper is where the bugs are.
CALL = re.compile(r'(\w[\w\->\.\[\]]*)\s*=\s*(?:\(.*?\)\s*)?(\w*realloc\w*)\s*\((.*)', re.I)

def args(argstr):
    """Split the call's arguments. A macro wrapper puts the pointer anywhere in
    the list (expat's REALLOC(parser, p, s) puts it second), so every argument
    is a candidate rather than just the first."""
    depth, cur, out = 0, '', []
    for ch in argstr:
        if ch in '([': depth += 1
        elif ch in ')]':
            if depth == 0: break
            depth -= 1
        elif ch == ',' and depth == 0:
            out.append(cur.strip()); cur = ''; continue
        cur += ch
    out.append(cur.strip())
    return [a for a in out if a]

def base(expr):
    # tag->buf.raw and tag->buf.str alias through a union; compare on the stem.
    m = re.match(r'^[\w\->\.\[\]]+$', expr)
    return expr if m else None

def scan(path, window=20):
    try: lines = open(path, errors='replace').read().split('\n')
    except OSError: return
    for i, ln in enumerate(lines):
        m = CALL.search(ln)
        if not m: continue
        new, fn, rest = m.group(1), m.group(2), m.group(3)
        j = i
        while ')' not in rest and j + 1 < len(lines):
            j += 1; rest += lines[j]
        cands = [a for a in (base(x) for x in args(rest)) if a and a != new]
        for old in cands:
            # The reallocated pointer is the argument the code repairs from the
            # result. Finding that line first is what distinguishes it from the
            # allocator handle and the size expression sitting next to it.
            stem = old.rsplit('.',1)[0] if '.' in old.rsplit('>',1)[-1] else old
            repair = None
            fix = re.compile(r'(?<![\w>])' + re.escape(stem) + r'(\.\w+)?\s*=\s*\(?[^=]*' + re.escape(new) + r'(?![\w])')
            for k in range(j+1, min(j+1+window, len(lines))):
                if fix.search(lines[k]): repair = k; break
            if repair is None: continue
            # Any read of the old pointer before that repair is a read of a value
            # realloc may already have deallocated.
            use = re.compile(r'(?<![\w>])' + re.escape(stem) + r'(?![\w(])')
            for k in range(j+1, repair):
                t = lines[k]
                if t.strip().startswith(('*','//','/*')): continue
                if 'free' in t: continue
                if use.search(t):
                    print(f"{path}:{k+1}: reads {stem!r} after {fn}() (line {i+1}), repaired only at line {repair+1}")
                    print(f"    {i+1}: {lines[i].strip()[:96]}")
                    print(f"    {k+1}: {t.strip()[:96]}")
                    print(f"    {repair+1}: {lines[repair].strip()[:96]}")
                    break
            break

for root in sys.argv[1:]:
    for dirpath, dirnames, files in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in ('.git','test','tests','deps','third_party')]
        for f in files:
            if f.endswith(('.c','.h')): scan(os.path.join(dirpath,f))
