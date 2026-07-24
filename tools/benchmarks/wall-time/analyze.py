#!/usr/bin/env python3
# Paired statistics for the real-TU benchmark. Accepts either the JSON written by
# real_tu_bench.py or a streamed run log (real_tu_run.log). Reports the nullsafe
# and analyzer overhead with a proper significance test on the log-ratio scale
# (correct because TU sizes span orders of magnitude and overhead is multiplicative).
import re, math, sys, os, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stats import desc, t_sf

def load(path):
    txt = open(path).read()
    rows = []
    if path.endswith(".json"):
        for r in json.loads(txt):
            rows.append((r["file"], r["baseline"]*1000, r["nullsafe"]*1000, r["analyzer"]*1000))
    else:  # streamed log lines: "[i/N] file  base=..ms ns=..ms anlz=..ms"
        for m in re.finditer(r'\]\s+(\S+)\s+base=\s*([\d.]+)ms\s+ns=\s*([\d.]+)ms\s+anlz=\s*([\d.]+)ms', txt):
            rows.append((m.group(1), float(m.group(2)), float(m.group(3)), float(m.group(4))))
    return rows

def logratio_test(pairs, label):
    lr = [math.log(y/x) for x, y in pairs]
    d = desc(lr); t = d['mean']/d['se']; p = t_sf(abs(t), d['n']-1)
    lo = 100*(math.exp(d['mean']-1.96*d['se'])-1); hi = 100*(math.exp(d['mean']+1.96*d['se'])-1)
    geo = 100*(math.exp(d['mean'])-1)
    print(f"  {label}: geomean {geo:+.2f}%  (95% CI {lo:+.2f}%..{hi:+.2f}%)  "
          f"t={t:.3f}, df={d['n']-1}, p={p:.4f} -> {'SIGNIFICANT' if p<0.05 else 'n.s.'}")

def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "real_tu_run.log")
    rows = load(path)
    n = len(rows)
    print(f"REAL clang/LLVM TUs (paired): n={n}   source={os.path.basename(path)}\n")
    print("== overhead on log-ratio scale (primary test) ==")
    logratio_test([(b, ns) for _, b, ns, _ in rows], "NULLSAFE vs baseline")
    logratio_test([(b, a) for _, b, _, a in rows], "ANALYZER vs baseline")
    print("\n== worst analyzer slowdowns ==")
    for f, b, ns, a in sorted(rows, key=lambda r: -r[3]/r[1])[:4]:
        print(f"  {f:<28} {a/b:7.1f}x   ({b:.0f}ms -> {a:.0f}ms)")

if __name__ == "__main__":
    main()
