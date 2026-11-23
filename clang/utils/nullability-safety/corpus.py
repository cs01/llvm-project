#!/usr/bin/env python3
"""Measure -fnullability-safety false positives and false negatives on real code.

The corpus (corpus.json) is a fixed set of public C and C++ projects pinned to
commits. Nothing in it was written for this analysis, so it shows how the
analysis behaves on code it has never seen.

False positives: `compare` runs a baseline and a candidate clang over the corpus
and reports every warning the candidate adds or removes. Warnings are matched
on file, diagnostic, message and the text of the source line, so the same
warning at a shifted line number is not reported as a change. Every added
warning needs a look before a change lands: it is either a newly found bug or a
new false positive.

False negatives: `mutate` deletes real null guards (`if (!p) return;` followed
by a dereference of p) one at a time, keeping line numbers, and checks that the
analysis now warns within the guarded region. The share of mutants caught is a
recall figure on real code, and each missed mutant is a concrete example of a
bug the analysis cannot see.

Usage:
  corpus.py snapshot --clang build/bin/clang -o nullable.json
  corpus.py diff old.json new.json
  corpus.py compare --old-clang /tmp/clang-base --new-clang build/bin/clang
  corpus.py mutate --clang build/bin/clang --sample 200

Project paths expand {repo} (this checkout), {build} (--build, default
{repo}/build), {corpus} (--corpus-root, default the checkout's parent
directory) and {root} (the project's own root).
"""

import argparse
import fnmatch
import glob
import json
import os
import random
import re
import shlex
import subprocess
import sys
import tempfile
from collections import Counter
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
DIAG_RE = re.compile(
    r"^(?P<file>.+?):(?P<line>\d+):(?P<col>\d+): (?P<kind>warning|error|fatal error): "
    r"(?P<msg>.*?)(?: \[(?P<flag>-W[^\]]+)\])?$"
)
GUARD_RE = re.compile(
    r"^\s*if\s*\(\s*(?:!\s*(?P<a>[A-Za-z_]\w*)|(?P<b>[A-Za-z_]\w*)\s*==\s*(?:NULL|nullptr|0))\s*\)\s*"
    r"(?P<body>(?:\{\s*)?(?:return\b[^;]*|continue|break|goto\s+\w+);\s*\}?)?\s*$"
)
NEXT_STMT_RE = re.compile(r"^\s*(?:return\b[^;]*|continue|break|goto\s+\w+);\s*$")
MUTATION_WINDOW = 40
DECL_LOOKBACK = 300
NON_POINTER_WRAPPERS = r"(?:Expected|ErrorOr|optional|Optional|StringRef|ArrayRef)\s*<"


def is_pointer_decl(lines, i, var):
    v = re.escape(var)
    raw = re.compile(r"[\w>)\]]\s*\*+\s*(?:const\s+)?(?:_Nullable\s+|_Nonnull\s+)?" + v + r"\s*(?:[=;,)\[]|$)")
    smart = re.compile(r"\b(?:unique_ptr|shared_ptr)\s*<.*>\s*&?\s*" + v + r"\s*(?:[=;,)({]|$)")
    other = re.compile(r"(?:\bauto\b(?!\s*\*)|" + NON_POINTER_WRAPPERS + r".*>)\s*&?\s*" + v + r"\s*(?:[=;,)({:]|$)")
    for j in range(i - 1, max(-1, i - DECL_LOOKBACK), -1):
        text = lines[j]
        if other.search(text):
            return False
        if raw.search(text) or smart.search(text):
            return True
    return False


def expand(s, ctx):
    for k, v in ctx.items():
        s = s.replace("{" + k + "}", v)
    return s


def load_projects(args):
    with open(args.manifest) as f:
        manifest = json.load(f)
    base = {
        "repo": REPO,
        "build": os.path.abspath(args.build or os.path.join(REPO, "build")),
        "corpus": os.path.abspath(args.corpus_root or os.path.dirname(REPO)),
    }
    projects = []
    for p in manifest["projects"]:
        if args.project and p["name"] not in args.project:
            continue
        ctx = dict(base)
        ctx["root"] = expand(p["root"], base)
        root = ctx["root"]
        if not os.path.isdir(root):
            print(f"skip {p['name']}: {root} not found", file=sys.stderr)
            continue
        if p.get("commit"):
            head = subprocess.run(["git", "-C", root, "rev-parse", "HEAD"],
                                  capture_output=True, text=True).stdout.strip()
            if head != p["commit"]:
                print(f"warning: {p['name']} is at {head[:12]}, manifest pins "
                      f"{p['commit'][:12]}; results are not comparable",
                      file=sys.stderr)
        projects.append((p, root, tu_commands(p, root, ctx)))
    return projects


def strip_pch(flags):
    out, i = [], 0
    while i < len(flags):
        if flags[i] == "-Xclang" and i + 1 < len(flags):
            if flags[i + 1] in ("-include-pch", "-include") and i + 3 < len(flags):
                i += 4
                continue
            if flags[i + 1] == "-fno-pch-timestamp":
                i += 2
                continue
        if flags[i] != "-Winvalid-pch":
            out.append(flags[i])
        i += 1
    return out


def tu_commands(p, root, ctx):
    lang = ["-x", "c++"] if p["lang"] == "c++" else ["-x", "c"]
    if "compdb" in p:
        with open(expand(p["compdb"], ctx)) as f:
            db = json.load(f)
        tus = []
        for entry in db:
            path = os.path.normpath(os.path.join(entry["directory"], entry["file"]))
            rel = os.path.relpath(path, root)
            if not any(fnmatch.fnmatch(rel, pat) for pat in p["files"]):
                continue
            argv = entry["arguments"] if "arguments" in entry else shlex.split(entry["command"])
            flags, skip = [], False
            for a in argv[1:]:
                if skip:
                    skip = False
                    continue
                if a in ("-o", "-MF", "-MT", "-MQ"):
                    skip = True
                    continue
                if a in ("-c", "-MD", "-MMD") or a == entry["file"] or a == path:
                    continue
                flags.append(a)
            tus.append((rel, entry["directory"], strip_pch(flags)))
        return sorted(tus)
    flags = lang + p.get("flags", [])
    rels = set()
    for pat in p["files"]:
        rels.update(os.path.relpath(f, root) for f in glob.glob(os.path.join(root, pat)))
    return [(rel, root, flags) for rel in sorted(rels)]


_resource_dirs = {}


def resource_dir(clang):
    if clang not in _resource_dirs:
        out = subprocess.run([clang, "-print-resource-dir"], capture_output=True, text=True)
        rd = out.stdout.strip()
        if not os.path.isdir(os.path.join(rd, "include")):
            ver = subprocess.run([clang, "--version"], capture_output=True, text=True).stdout
            m = re.search(r"clang version (\d+)", ver)
            if m:
                rd = os.path.join(REPO, "build", "lib", "clang", m.group(1))
        _resource_dirs[clang] = rd
    return _resource_dirs[clang]


def preprocess(clang, directory, flags, path):
    cmd = [clang, "-resource-dir", resource_dir(clang), *flags, "-E", "-P", path]
    return subprocess.run(cmd, cwd=directory, capture_output=True, text=True).stdout


def run_clang(clang, mode, directory, flags, path):
    cmd = [clang, "-resource-dir", resource_dir(clang), *flags, "-fsyntax-only",
           "-fno-color-diagnostics", "-fno-caret-diagnostics", "-Wno-everything", "-Wno-error",
           "-fnullability-safety", f"-fnullability-default={mode}", "-Wnullability-safety", path]
    out = subprocess.run(cmd, cwd=directory, capture_output=True, text=True)
    diags = [] if out.returncode == 0 else [{"file": path, "line": "0", "col": "0",
                                             "kind": "error", "msg": "clang exited nonzero",
                                             "flag": None}]
    for line in out.stderr.splitlines():
        m = DIAG_RE.match(line)
        if m and (m["kind"] != "warning" or (m["flag"] or "").startswith("-Wnullability")):
            diags.append(m.groupdict())
    return diags


_line_cache = {}


def source_line(path, line):
    if path not in _line_cache:
        try:
            with open(path, errors="replace") as f:
                _line_cache[path] = f.read().splitlines()
        except OSError:
            _line_cache[path] = []
    lines = _line_cache[path]
    return " ".join(lines[line - 1].split()) if 0 < line <= len(lines) else ""


def snapshot(args, clang):
    records = []
    stats = {}
    for p, root, tus in load_projects(args):
        def one(tu):
            rel, directory, flags = tu
            return tu, run_clang(clang, args.mode, directory, flags, os.path.join(root, rel))

        with ThreadPoolExecutor(args.jobs) as pool:
            results = list(pool.map(one, tus))
        seen, errors = set(), 0
        for (rel, directory, _), diags in results:
            if any(d["kind"] != "warning" for d in diags):
                errors += 1
                continue
            for d in diags:
                path = os.path.normpath(os.path.join(directory, d["file"]))
                if not path.startswith(root + os.sep):
                    continue
                loc = (path, d["line"], d["col"], d["msg"])
                if loc in seen:
                    continue
                seen.add(loc)
                records.append({
                    "project": p["name"],
                    "file": os.path.relpath(path, root),
                    "line": int(d["line"]),
                    "col": int(d["col"]),
                    "flag": d["flag"] or "",
                    "msg": d["msg"],
                    "text": source_line(path, int(d["line"])),
                })
        count = sum(r["project"] == p["name"] for r in records)
        stats[p["name"]] = {"tus": len(tus), "tus_with_errors": errors, "warnings": count}
        print(f"{p['name']:14} {len(tus):4} TUs  {errors:3} failed  {count:5} warnings",
              file=sys.stderr)
    return {"clang": clang, "mode": args.mode, "stats": stats, "warnings": records}


def key(r):
    return (r["project"], r["file"], r["flag"], r["msg"], r["text"])


def diff(old, new):
    oc = Counter(key(r) for r in old["warnings"])
    nc = Counter(key(r) for r in new["warnings"])
    extra_new = nc - oc
    extra_old = oc - nc
    added, removed = [], []
    for r in new["warnings"]:
        if extra_new[key(r)] > 0:
            extra_new[key(r)] -= 1
            added.append(r)
    for r in old["warnings"]:
        if extra_old[key(r)] > 0:
            extra_old[key(r)] -= 1
            removed.append(r)
    for title, rows in (("added", added), ("removed", removed)):
        print(f"\n{len(rows)} {title} warnings")
        for r in rows:
            print(f"  {r['project']}/{r['file']}:{r['line']}:{r['col']}: {r['msg']} [{r['flag']}]")
            print(f"      {r['text']}")
    print(f"\ntotal: {len(old['warnings'])} -> {len(new['warnings'])} "
          f"(+{len(added)} -{len(removed)})")
    return added, removed


def find_guards(path):
    try:
        with open(path, errors="replace") as f:
            lines = f.read().splitlines()
    except OSError:
        return []
    guards = []
    for i, text in enumerate(lines):
        m = GUARD_RE.match(text)
        if not m or text.rstrip().endswith("\\"):
            continue
        var = m.group("a") or m.group("b")
        if not is_pointer_decl(lines, i, var):
            continue
        span = [i]
        if not m.group("body"):
            if i + 1 < len(lines) and NEXT_STMT_RE.match(lines[i + 1]):
                span.append(i + 1)
            else:
                continue
        deref = re.compile(r"(?<![\w.>])" + re.escape(var) + r"\s*->|\*\s*" + re.escape(var) + r"\b(?!\s*[\w(])")
        end = min(len(lines), span[-1] + 1 + MUTATION_WINDOW)
        for j in range(span[-1] + 1, end):
            if re.match(r"^\}", lines[j]):
                break
            if re.search(r"\b" + re.escape(var) + r"\s*=[^=]", lines[j]):
                break
            if deref.search(lines[j]):
                guards.append({"span": span, "var": var, "deref_line": j + 1,
                               "text": " ".join(lines[i].split())})
                break
    return guards


def mutate(args):
    candidates = []
    for p, root, tus in load_projects(args):
        for rel, directory, flags in tus:
            path = os.path.join(root, rel)
            for g in find_guards(path):
                candidates.append((p["name"], root, rel, directory, flags, g))
    rng = random.Random(args.seed)
    sample = candidates if len(candidates) <= args.sample else rng.sample(candidates, args.sample)
    print(f"{len(candidates)} candidate guards, mutating {len(sample)}", file=sys.stderr)

    def one(c):
        name, root, rel, directory, flags, g = c
        path = os.path.join(root, rel)
        with open(path, errors="replace") as f:
            lines = f.read().split("\n")
        before = run_clang(args.clang, args.mode, directory, flags + ["-I", os.path.dirname(path)], path)
        if any(d["kind"] != "warning" for d in before):
            return c, "error", []
        for i in g["span"]:
            lines[i] = ""
        fd, tmp = tempfile.mkstemp(suffix=os.path.splitext(path)[1], dir=os.path.dirname(path),
                                   prefix=".nullsafe-mutant-")
        inc = flags + ["-I", os.path.dirname(path)]
        try:
            with os.fdopen(fd, "w") as f:
                f.write("\n".join(lines))
            mutant_pp = preprocess(args.clang, directory, inc, tmp).replace(os.path.basename(tmp), os.path.basename(path))
            if mutant_pp == preprocess(args.clang, directory, inc, path):
                return c, "not-compiled", []
            after = run_clang(args.clang, args.mode, directory, inc, tmp)
        finally:
            os.unlink(tmp)
        if any(d["kind"] != "warning" for d in after):
            return c, "error", []
        base = {(d["line"], d["col"], d["msg"]) for d in before
                if os.path.basename(d["file"]) == os.path.basename(path)}
        lo, hi = g["span"][-1] + 1, g["span"][-1] + 1 + MUTATION_WINDOW
        new = [d for d in after if os.path.basename(d["file"]) == os.path.basename(tmp)
               and (d["line"], d["col"], d["msg"]) not in base and lo <= int(d["line"]) <= hi]
        return c, "caught" if new else "missed", new

    with ThreadPoolExecutor(args.jobs) as pool:
        results = list(pool.map(one, sample))
    tally = Counter(status for _, status, _ in results)
    valid = tally["caught"] + tally["missed"]
    print(f"\ncaught {tally['caught']} / {valid} mutants"
          f" ({100.0 * tally['caught'] / valid:.1f}% recall)" if valid else "\nno valid mutants")
    if tally["error"]:
        print(f"{tally['error']} mutants skipped (file did not compile)")
    if tally["not-compiled"]:
        print(f"{tally['not-compiled']} mutants skipped (guard is in preprocessor-excluded code)")
    by_project = Counter((c[0], s) for c, s, _ in results)
    for name in sorted({c[0] for c, _, _ in results}):
        cc, mm = by_project[(name, "caught")], by_project[(name, "missed")]
        if cc + mm:
            print(f"  {name:14} {cc}/{cc + mm} ({100.0 * cc / (cc + mm):.0f}%)")
    missed = [(c, n) for c, s, n in results if s == "missed"]
    if missed:
        print(f"\nmissed (removing the guard produced no warning):")
        for (name, root, rel, _, _, g), _ in sorted(missed, key=lambda x: (x[0][0], x[0][2], x[0][5]["span"][0])):
            print(f"  {name}/{rel}:{g['span'][0] + 1}: {g['text']}   (deref at {g['deref_line']})")
    if args.output:
        with open(args.output, "w") as f:
            json.dump([{"project": c[0], "file": c[2], "line": c[5]["span"][0] + 1,
                        "guard": c[5]["text"], "var": c[5]["var"], "status": s,
                        "warnings": [d["msg"] for d in n]} for c, s, n in results], f, indent=1)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--manifest", default=os.path.join(HERE, "corpus.json"))
    ap.add_argument("--build")
    ap.add_argument("--corpus-root")
    ap.add_argument("--project", action="append", help="limit to these projects")
    ap.add_argument("--mode", default="nullable", choices=["nullable", "nonnull", "unspecified"])
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count())
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("snapshot")
    s.add_argument("--clang", required=True)
    s.add_argument("-o", "--output", required=True)
    d = sub.add_parser("diff")
    d.add_argument("old")
    d.add_argument("new")
    c = sub.add_parser("compare")
    c.add_argument("--old-clang", required=True)
    c.add_argument("--new-clang", required=True)
    c.add_argument("-o", "--output", help="write the candidate snapshot here")
    m = sub.add_parser("mutate")
    m.add_argument("--clang", required=True)
    m.add_argument("--sample", type=int, default=200)
    m.add_argument("--seed", type=int, default=1)
    m.add_argument("-o", "--output")
    args = ap.parse_args()

    if args.cmd == "snapshot":
        snap = snapshot(args, os.path.abspath(args.clang))
        with open(args.output, "w") as f:
            json.dump(snap, f, indent=1)
    elif args.cmd == "diff":
        with open(args.old) as f:
            old = json.load(f)
        with open(args.new) as f:
            new = json.load(f)
        added, _ = diff(old, new)
        sys.exit(1 if added else 0)
    elif args.cmd == "compare":
        old = snapshot(args, os.path.abspath(args.old_clang))
        new = snapshot(args, os.path.abspath(args.new_clang))
        if args.output:
            with open(args.output, "w") as f:
                json.dump(new, f, indent=1)
        added, _ = diff(old, new)
        sys.exit(1 if added else 0)
    elif args.cmd == "mutate":
        args.clang = os.path.abspath(args.clang)
        mutate(args)


if __name__ == "__main__":
    main()
