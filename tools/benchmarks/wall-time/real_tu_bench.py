#!/usr/bin/env python3
# Samples real clang/LLVM translation units from compile_commands.json and
# times each under three modes. Paired design: every TU measured in every mode,
# so per-TU size variance cancels out. Reports per-TU best-of-K wall time
# (min reduces scheduler noise) and aggregates for a paired significance test.
import json, os, subprocess, sys, shlex, time, random

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.environ.get("BUILD_DIR", os.path.join(HERE, "..", "..", "..", "build"))
CC = os.environ.get("COMPILE_COMMANDS", os.path.join(BUILD, "compile_commands.json"))
FORK = os.environ.get("CLANG", os.path.join(BUILD, "bin", "clang"))
NULLSAFE = ["-fflow-sensitive-nullability", "-fnullability-default=nullable"]
PIN = ["taskset", "-c", "4"]
N_TU = int(sys.argv[1]) if len(sys.argv) > 1 else 40
K = int(sys.argv[2]) if len(sys.argv) > 2 else 3   # reps per TU per mode (take min)
SEED = 1234

def load():
    db = json.load(open(CC))
    tus = []
    for e in db:
        f = e.get("file", "")
        if not (f.endswith(".cpp") or f.endswith(".c") or f.endswith(".cc")):
            continue
        cmd = e.get("command") or " ".join(e.get("arguments", []))
        if not cmd: continue
        tus.append((e["directory"], f, shlex.split(cmd)))
    return tus

def strip_out(args):
    # drop -o X and -c and -MF/-MT/-MD deps and the source file; return (flags, src)
    out=[]; skip=0; src=None
    for i,a in enumerate(args):
        if skip: skip-=1; continue
        if a=="-o": skip=1; continue
        if a in ("-c",): continue
        if a in ("-MF","-MT","-MQ","-MJ"): skip=1; continue
        if a in ("-MD","-MMD","-MP"): continue
        if a.endswith((".cpp",".c",".cc",".o")) and (a==args[-1] or a.endswith((".cpp",".c",".cc"))):
            if a.endswith((".cpp",".c",".cc")): src=a; continue
        out.append(a)
    return out, src

def build_cmds(directory, args):
    # args[0] is the compiler; replace with FORK
    flags, src = strip_out(args[1:])
    if src is None: return None
    base = [FORK] + flags
    # frontend-only isolation: syntax-only
    syn = base + ["-fsyntax-only", src]
    syn_ns = base + ["-fsyntax-only"] + NULLSAFE + [src]
    analyze = base + ["--analyze", "-Xclang", "-analyzer-output=text", src]
    return dict(baseline=syn, nullsafe=syn_ns, analyzer=analyze)

def timed(directory, cmd, k):
    best=None
    for _ in range(k):
        t0=time.perf_counter()
        r=subprocess.run(PIN+cmd, cwd=directory, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL)
        dt=time.perf_counter()-t0
        if best is None or dt<best: best=dt
    return best, r.returncode

def main():
    tus=load()
    rnd=random.Random(SEED)
    rnd.shuffle(tus)
    picked=[]
    results=[]
    for directory,f,args in tus:
        if len(picked)>=N_TU: break
        cmds=build_cmds(directory,args)
        if not cmds: continue
        # warm the cache / verify baseline compiles cleanly (exit 0), else skip
        b,rc=timed(directory,cmds["baseline"],1)
        if rc!=0: continue
        row={"file":os.path.basename(f)}
        for mode in ("baseline","nullsafe","analyzer"):
            reps = 1 if mode=="analyzer" else K   # analyzer is expensive; 1 rep
            t,rc=timed(directory,cmds[mode],reps)
            row[mode]=t
            row[mode+"_rc"]=rc
        picked.append(f); results.append(row)
        print(f"[{len(picked)}/{N_TU}] {row['file']:<28} "
              f"base={row['baseline']*1000:7.1f}ms  ns={row['nullsafe']*1000:7.1f}ms  "
              f"anlz={row['analyzer']*1000:7.1f}ms", flush=True)
    outp = os.path.join(HERE, "real_tu.json")
    json.dump(results, open(outp, "w"), indent=1)
    print(f"\nwrote {outp} with {len(results)} TUs")

if __name__=="__main__":
    main()
