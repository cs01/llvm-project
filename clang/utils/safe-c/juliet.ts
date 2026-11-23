// Juliet C v1.3 scoring harness: builds BAD/GOOD variants per test case with a given clang and tallies per-CWE results.
import { readdirSync, readFileSync, mkdirSync, writeFileSync, rmSync, statSync } from "fs";
import { join, basename, dirname } from "path";

const args = process.argv.slice(2);
const opt = (n: string, d?: string) => { const i = args.indexOf("--" + n); return i < 0 ? d : args[i + 1]; };
const cc = opt("cc");
if (!cc) { console.error('usage: bun juliet.ts --cc <clang> [--sysflags "<all compiles>"] --flags "<test sources only>" [--cwe 476,121] [--run] [--limit N] [--jobs 4] [--out results.json]'); process.exit(2); }
const extra = (opt("flags", "") as string).split(/\s+/).filter(Boolean);
// sysflags (e.g. -isysroot) apply to every compile and link; flags only to test-case sources,
// so the support files build like an unannotated system library would.
const sys = (opt("sysflags", "") as string).split(/\s+/).filter(Boolean);
// Compiler for the plain reference builds (see build()).
const baselineCc = (opt("baseline-cc", "/usr/bin/clang") as string);
const cwes = (opt("cwe", "476,121,122,416,415,457,190,124,126,127,690") as string).split(",").map(s => s.trim());
const doRun = args.includes("--run");
const limit = opt("limit") ? parseInt(opt("limit")!) : Infinity;
const jobs = Math.min(4, Math.max(1, parseInt(opt("jobs", "4")!))); // hard cap: shared 16 GB machine
const outFile = opt("out");
// The suite is not vendored (146 MB); point JULIET_ROOT at a dir holding suite/C from
// https://samate.nist.gov/SARD/downloads/test-suites/2017-10-01-juliet-test-suite-for-c-cplusplus-v1-3.zip
const ROOT = process.env.JULIET_ROOT ?? import.meta.dir;
const TC = join(ROOT, "suite/C/testcases");
const SUP = join(ROOT, "suite/C/testcasesupport");
const BUILD = join(ROOT, "build", String(process.pid));
mkdirSync(BUILD, { recursive: true });

function walk(d: string, out: string[]) {
  for (const e of readdirSync(d)) { const p = join(d, e); statSync(p).isDirectory() ? walk(p, out) : out.push(p); }
}

type Case = { cwe: string; key: string; files: string[] };
function collect(): Case[] {
  const cases: Case[] = [];
  for (const dir of readdirSync(TC)) {
    const m = dir.match(/^CWE(\d+)_/);
    if (!m || !cwes.includes(m[1])) continue;
    const files: string[] = []; walk(join(TC, dir), files);
    const groups = new Map<string, string[]>();
    for (const f of files.filter(f => f.endsWith(".c")).sort()) {
      // strip the a,b,c part suffix so multi-file cases group; key includes dir (s01 etc.)
      const key = join(dirname(f), basename(f, ".c").replace(/(_\d+)[a-z]$/, "$1"));
      (groups.get(key) ?? groups.set(key, []).get(key)!).push(f);
    }
    for (const [key, fs] of groups) {
      // Windows-only: any member mentioning w32 or windows.h drops the whole case
      if (fs.some(f => /w32/.test(basename(f)) || /windows\.h|\bw32/i.test(readFileSync(f, "latin1")))) continue;
      cases.push({ cwe: m[1], key: basename(key), files: fs });
    }
  }
  return cases;
}

const sh = async (cmd: string[], o: { cwd?: string; timeoutMs?: number; input?: string } = {}) => {
  const p = Bun.spawn(cmd, { cwd: o.cwd, stdin: o.input !== undefined ? new TextEncoder().encode(o.input) : "ignore", stdout: "pipe", stderr: "pipe" });
  let timedOut = false;
  const t = o.timeoutMs ? setTimeout(() => { timedOut = true; p.kill("SIGKILL"); }, o.timeoutMs) : null;
  const [out, err] = await Promise.all([new Response(p.stdout).text(), new Response(p.stderr).text()]);
  await p.exited; if (t) clearTimeout(t);
  return { code: p.exitCode, signal: p.signalCode as string | null, out, err, timedOut };
};

// No -w here: it would also silence warnings the flags promote with -Werror, and
// a gate that cannot see compile-time rejections reports 0% forever.
const base = ["-I", SUP, "-DINCLUDEMAIN", ...sys, ...extra];
const supBase = ["-w", "-I", SUP, ...sys];
const firstError = (s: string) => (s.split("\n").find(l => /error|undefined|ld:/.test(l)) ?? s.split("\n")[0] ?? "").trim().slice(0, 300);

// plain = true builds without the flags under test (and with the baseline
// compiler), to tell a compiler-inserted trap from one the platform raises on
// its own (macOS malloc traps on double free, for example).
async function build(c: Case, variant: "bad" | "good", dir: string, support: string[], plain = false) {
  const def = variant === "bad" ? "-DOMITGOOD" : "-DOMITBAD";
  const objs: string[] = [];
  const compiler = plain ? baselineCc : cc!;
  const flags = plain ? ["-I", SUP, "-DINCLUDEMAIN", ...sys] : base;
  for (const [i, f] of c.files.entries()) {
    const o = join(dir, `${plain ? "plain-" : ""}${variant}${i}.o`);
    const r = await sh([compiler, ...flags, def, "-I", dirname(f), "-c", f, "-o", o]);
    if (r.code !== 0) return { stage: "compile" as const, err: firstError(r.err) };
    objs.push(o);
  }
  const exe = join(dir, (plain ? "plain-" : "") + variant);
  const r = await sh([compiler, ...sys, ...objs, ...support, "-lm", "-lpthread", "-o", exe]);
  if (r.code !== 0) return { stage: "link" as const, err: firstError(r.err) };
  return { stage: "ok" as const, exe, err: "" };
}

async function runExe(exe: string, cwd: string, cwe: string) {
  // Juliet bad paths read their bad value from stdin (fgets/fscanf): a zero
  // divisor for CWE 369, an out-of-range index or size for the buffer CWEs.
  const input = (cwe === "369" ? "0\n" : "100\n").repeat(64);
  const r = await sh([ exe ], {cwd, timeoutMs : 5000, input});
  return { signal: r.timedOut ? "TIMEOUT" : r.signal, code: r.code };
}

async function doCase(c: Case, ioObj: string, thrObj: string) {
  const dir = join(BUILD, `${c.cwe}_${c.key}`.replace(/[^\w.-]/g, "_"));
  mkdirSync(dir, { recursive: true });
  const needThr = c.files.some(f => /std_thread|stdThread/.test(readFileSync(f, "latin1")));
  const support = needThr ? [ioObj, thrObj] : [ioObj];
  const res: any = { cwe: c.cwe, case: c.key, files: c.files.map(f => basename(f)) };
  const b = await build(c, "bad", dir, support);
  if (b.stage === "compile") { res.bad = "rejected"; res.badErr = b.err; }
  else if (b.stage === "link") { res.bad = "linkfail"; res.badErr = b.err; }
  else if (!doRun) res.bad = "compiled";
  else {
    const r = await runExe(b.exe!, dir, c.cwe);
    // Only compiler-inserted traps count (brk -> SIGTRAP, ud2 -> SIGILL). A SIGSEGV or
    // SIGABRT is the UB happening to crash, not a safety check, so it scores as missed.
    if (r.signal === "SIGTRAP" || r.signal === "SIGILL") { res.bad = "trapped"; res.badSignal = r.signal; } else { res.bad = "missed"; res.badExit = r.signal ?? r.code; }
    // A trap the plain build also hits is the platform's, not ours.
    if (res.bad === "trapped") {
      const p = await build(c, "bad", dir, support, true);
      if (p.stage === "ok") {
        const pr = await runExe(p.exe!, dir, c.cwe);
        if (pr.signal === r.signal) { res.bad = "missed"; res.badExit = "platform " + r.signal; }
      }
    }
  }
  const g = await build(c, "good", dir, support);
  if (g.stage !== "ok") { res.good = "false-positive"; res.goodErr = `${g.stage}: ${g.err}`; }
  else if (!doRun) res.good = "accepted";
  else {
    const r = await runExe(g.exe!, dir, c.cwe);
    if (r.signal || r.code !== 0) { res.good = "false-positive"; res.goodErr = `run: ${r.signal ?? "exit " + r.code}`; } else res.good = "accepted";
  }
  rmSync(dir, { recursive: true, force: true });
  return res;
}

let all = collect();
const counts: Record<string, number> = {};
for (const c of all) counts[c.cwe] = (counts[c.cwe] ?? 0) + 1;
if (isFinite(limit)) { // --limit applies per CWE
  const seen: Record<string, number> = {};
  all = all.filter(c => (seen[c.cwe] = (seen[c.cwe] ?? 0) + 1) <= limit);
}
// support objects built once; they do not depend on OMITBAD/OMITGOOD
const ioObj = join(BUILD, "io.o"), thrObj = join(BUILD, "std_thread.o");
for (const [s, o] of [["io.c", ioObj], ["std_thread.c", thrObj]]) {
  const r = await sh([cc!, ...supBase, "-c", join(SUP, s), "-o", o]);
  if (r.code !== 0) { console.error("support build failed:", r.err); process.exit(1); }
}
const results: any[] = []; let next = 0;
await Promise.all(Array.from({ length: jobs }, async () => {
  while (next < all.length) results.push(await doCase(all[next++], ioObj, thrObj));
}));
rmSync(BUILD, { recursive: true, force: true });

const pct = (a: number, b: number) => (b ? (100 * a / b).toFixed(1) + "%" : "-");
// strict%: of the cases whose good() is accepted, the share whose bad() is
// caught. A rejection that also rejects good() says nothing about the bug.
const rows: string[][] = [["CWE", "cases", "rej", "trap", "miss", "other", "g.acc", "g.fp", "bad%", "good%", "strict%"]];
for (const w of cwes) {
  const rs = results.filter(r => r.cwe === w); if (!rs.length) continue;
  const n = (k: string, v: string) => rs.filter(r => r[k] === v).length;
  const rej = n("bad", "rejected"), tr = n("bad", "trapped"), ms = n("bad", "missed");
  const acc = n("good", "accepted");
  const pairs = rs.filter(r => r.good === "accepted");
  const strict = pairs.filter(r => r.bad === "rejected" || r.bad === "trapped").length;
  rows.push([w, String(rs.length), String(rej), String(tr), String(ms), String(rs.length - rej - tr - ms), String(acc), String(n("good", "false-positive")), pct(rej + tr, rs.length), pct(acc, rs.length), pct(strict, pairs.length)]);
}
const wd = rows[0].map((_, i) => Math.max(...rows.map(r => r[i].length)));
for (const r of rows) console.log(r.map((c, i) => c.padStart(wd[i])).join("  "));
console.log("(other = bad compiled-not-run or link failure; total C cases per CWE: " + JSON.stringify(counts) + ")");
// Paths relative to the suite: results files are committed and must not name a home dir.
if (outFile) writeFileSync(outFile, JSON.stringify(results.sort((a, b) => a.case.localeCompare(b.case)), null, 1).split(ROOT + "/").join(""));
