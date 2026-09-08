# Find the expat storeRawNames shape mechanically: realloc's argument pointer
# read again after the call, before it is reassigned. Text-level and therefore
# noisy; it is a triage filter, not an oracle.
import re, sys, os

# Case-insensitive: projects wrap realloc in an uppercase macro (expat's
# REALLOC, redis's s_realloc_usable), and the wrapper is where the bugs are.
# The reallocator is often reached through a struct field rather than named
# directly: cJSON calls p->hooks.reallocate(...), expat's REALLOC expands to
# parser->m_mem.realloc_fcn(...). Without the optional member prefix below this
# reports zero calls on those trees, which reads exactly like a clean result.
CALL = re.compile(r'(\w[\w\->\.\[\]]*)\s*=\s*(?:\(.*?\)\s*)?'
                  r'((?:\w[\w\[\]]*\s*(?:->|\.)\s*)*\w*realloc\w*)\s*\((.*)', re.I)

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

def on_failure_path(lines, guard, start, k):
    """Is line k inside the body of an `if (!new)` between start and k?

    Only inside. A guard whose body returns -- `if (t == NULL) return FALSE;` --
    leaves everything after it on the SUCCESS path, which is where expat's
    defect lives. Confusing the two hides the real finding, so the block extent
    is computed rather than assumed."""
    for g in range(start, k + 1):
        if not guard.search(lines[g]):
            continue
        if g == k:            # `if( pOut==0 ) return pIn;` -- read is the body
            return True
        if '{' in lines[g]:
            depth, end = 0, None
            for e in range(g, len(lines)):
                depth += lines[e].count('{') - lines[e].count('}')
                if depth <= 0 and e > g:
                    end = e
                    break
            if end is not None and g < k <= end:
                return True
        else:
            # Braceless. If the body sits on the guard's own line --
            # `if( !zNew ) return SQLITE_NOMEM;`, which is sqlite's house style
            # -- then the next line is already the success path, and treating it
            # as guarded hides the real defect there.
            tail = lines[g].split(')', 1)[-1].strip()
            if not tail and k == g + 1:
                return True
    return False

def union_siblings(lines, a, b):
    """Are members a and b arms of one union in this file? Stem matching is
    only sound for a union, where the two names denote the same storage."""
    if a == b:
        return True
    depth, inu, seen = 0, False, set()
    for ln in lines:
        if re.search(r'\bunion\b[^;]*\{', ln):
            inu, depth, seen = True, ln.count('{') - ln.count('}'), set()
            continue
        if inu:
            depth += ln.count('{') - ln.count('}')
            for m in re.finditer(r'(\w+)\s*(?:\[[^\]]*\])?\s*;', ln):
                seen.add(m.group(1))
            if depth <= 0:
                if a in seen and b in seen:
                    return True
                inu = False
    return False

COVERAGE = {'files': 0, 'calls': 0, 'candidates': 0, 'hits': 0}

def scan(path, window=20):
    COVERAGE['files'] += 1
    try: lines = open(path, errors='replace').read().split('\n')
    except OSError: return
    for i, ln in enumerate(lines):
        m = CALL.search(ln)
        if not m: continue
        COVERAGE['calls'] += 1
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
            # `p->z = p->zM = realloc(..., p->z, n)` repairs the old name on the
            # call line, so it is never stale. Look there before looking after.
            if re.search(r'(?<![\w>])' + re.escape(old) + r'\s*=[^=]', lines[i]):
                continue
            repair = None
            fix = re.compile(r'(?<![\w>])' + re.escape(stem) + r'(\.\w+)?\s*='
                             r'\s*(\([^)]*\)\s*)?&?\s*'
                             r'(?<![\w])' + re.escape(new) + r'(?![\w(])')
            for k in range(j+1, min(j+1+window, len(lines))):
                if fix.search(lines[k]): repair = k; break
            if repair is None: continue
            COVERAGE['candidates'] += 1
            # Any read of the old pointer before that repair is a read of a value
            # realloc may already have deallocated.
            use = re.compile(r'(?<![\w>])' + re.escape(stem) + r'(?![\w(])')
            # Reads on the failure path are correct by realloc's contract: when
            # it returns NULL the old block is untouched and still has to be
            # freed. redis rdb.c does exactly this.
            failguard = re.compile(r'\bif\s*\(\s*(!\s*' + re.escape(new) +
                                   r'|' + re.escape(new) + r'\s*==\s*(NULL|0)\b)')
            for k in range(j+1, repair):
                t = lines[k]
                if t.strip().startswith(('*','//','/*')): continue
                if 'free' in t: continue
                if on_failure_path(lines, failguard, j + 1, k):
                    continue
                mu = use.search(t)
                if not mu: continue
                after = t[mu.end():]
                # An assignment *to* the name is a write, not a read of the
                # freed value. zlib's pufftest.c does `buf = NULL;`.
                if re.match(r'(\.\w+)?\s*=[^=]', after): continue
                # Stem expansion exists for unions -- expat reads tag->buf.str
                # after reallocating tag->buf.raw. Outside a union, a sibling
                # member is a different object: values.values_num is not
                # values.values, and g.done[i].len is not g.done[i].vec.
                mem = re.match(r'\.(\w+)', after)
                if mem and stem != old:
                    oldmem = old[len(stem):].lstrip('.')
                    if not union_siblings(lines, oldmem, mem.group(1)):
                        continue
                COVERAGE['hits'] += 1
                if True:
                    print(f"{path}:{k+1}: reads {stem!r} after {fn}() (line {i+1}), repaired only at line {repair+1}")
                    print(f"    {i+1}: {lines[i].strip()[:96]}")
                    print(f"    {k+1}: {t.strip()[:96]}")
                    print(f"    {repair+1}: {lines[repair].strip()[:96]}")
                    break
            break

# A detector that examined nothing prints nothing, which is indistinguishable
# from a clean tree. It is the failure mode that makes a gate useless, and this
# one hit it for real: ~/git/postgres is a TypeScript client with no C in it,
# and the sweep recorded it as swept clean. So always report what was looked at.
for root in sys.argv[1:]:
    for dirpath, dirnames, files in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in ('.git','test','tests','deps','third_party')]
        for f in files:
            if f.endswith(('.c','.h')): scan(os.path.join(dirpath,f))

    c = COVERAGE
    note = ''
    if c['files'] == 0:
        note = '  <-- NO C FILES: this tree was not checked'
    elif c['calls'] == 0:
        # Every allocator this knows about has "realloc" in the name. A codebase
        # that renames it (postgres's repalloc, an in-house arena) reads as clean
        # and is not. Name the wrapper and re-run before believing a zero.
        note = '  <-- NO realloc-SHAPED CALLS: check what this tree names its reallocator'
    print(f"[coverage] {root}: {c['files']} C files, {c['calls']} realloc-shaped calls, "
          f"{c['candidates']} repaired-pointer sites, {c['hits']} hits{note}", file=sys.stderr)
    for k in c: c[k] = 0
