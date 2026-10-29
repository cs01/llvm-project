// Runs only the lit tests that can observe what changed since they last ran.
//
// `record` runs lit with per-test coverage on a build compiled with function
// entry coverage (see entrycov.c) and stores, per test, the functions its
// processes entered, plus a snapshot of every object file's atoms. `select`
// diffs the rebuilt objects against that snapshot and picks the tests that
// entered a changed function. A test that never entered a function cannot
// observe a change to that function's body; everything this cannot reason
// about (changed data, init code, uninstrumented code, runtime-read files,
// lit configuration) selects every test instead.
//
// usage:
//   bun semtest.ts record -C <build> <lit test paths...>
//   bun semtest.ts select -C <build> [--run] <lit test paths...>
import { spawnSync } from "child_process";
import { createHash } from "crypto";
import { existsSync, mkdirSync, readdirSync, readFileSync, rmSync, statSync, writeFileSync } from "fs";
import { join, relative, resolve } from "path";
import { readAtoms, type Atom } from "./macho.ts";

const args = process.argv.slice(2);
const mode = args.shift();
const opt = (name: string) => {
  const i = args.indexOf(name);
  return i < 0 ? undefined : args.splice(i, 2)[1];
};
const flag = (name: string) => {
  const i = args.indexOf(name);
  return i >= 0 && !!args.splice(i, 1);
};
const build = resolve(opt("-C") ?? ".");
const run = flag("--run");
const paths = args.map((p) => resolve(p));
// The lit suite holding the first path: test names are relative to it.
const suiteRoot = (() => {
  for (let d = paths[0] ?? ""; d !== "/"; d = resolve(d, ".."))
    if (existsSync(join(d, "lit.cfg.py"))) return d;
  throw new Error("no lit.cfg.py above " + paths[0]);
})();
const state = join(build, ".semtest");
const log = (...a: unknown[]) => console.error("semtest:", ...a);

// Sections whose contents never change what a test observes on their own:
// unwind info and profile bookkeeping move whenever code does.
const ignoredSection = (s: string) =>
  s.startsWith("__LD,") || s.startsWith("__DWARF,") || s.startsWith("__DATA,__llvm_") || s === "__TEXT,__eh_frame";
const initSection = (s: string) => /__mod_init_func|__init_offsets|__mod_term_func/.test(s);

// ---- object snapshots ------------------------------------------------------

interface ObjSnap { mtime: number; size: number; atoms: Record<string, string>; }
const objKey = (obj: string) => createHash("sha1").update(obj).digest("hex").slice(0, 16);
const snapPath = (obj: string) => join(state, "objs", objKey(obj) + ".json");

// Named atoms by symbol, local ones qualified so two files' statics differ;
// all of an object's init-array atoms fold into one "<init>" entry.
function summarize(atoms: Atom[]): Record<string, string> {
  const out: Record<string, string> = {};
  const init = createHash("sha1");
  for (const a of atoms) {
    if (initSection(a.section)) init.update(a.hash);
    else if (a.name !== "<anon>" && !ignoredSection(a.section))
      out[(a.code ? "T " : "D ") + a.name] = a.hash.slice(0, 16);
  }
  out["<init>"] = init.digest("hex").slice(0, 16);
  return out;
}

function objects(): string[] {
  const out: string[] = [];
  const walk = (d: string) => {
    for (const e of readdirSync(d, { withFileTypes: true })) {
      if (e.isDirectory()) { if (e.name !== ".semtest") walk(join(d, e.name)); }
      else if (e.name.endsWith(".o")) out.push(join(d, e.name));
    }
  };
  for (const d of ["lib", "tools", "utils"]) if (existsSync(join(build, d))) walk(join(build, d));
  return out;
}

function snapshot(obj: string): ObjSnap {
  const st = statSync(obj);
  return { mtime: st.mtimeMs, size: st.size, atoms: summarize(readAtoms(readFileSync(obj))) };
}

// Files read at run time rather than linked in: the resource directory
// (builtin headers, module maps) and lit's generated site configs.
function runtimeInputs(): Record<string, number> {
  const out: Record<string, number> = {};
  const walk = (d: string) => {
    if (!existsSync(d)) return;
    for (const e of readdirSync(d, { withFileTypes: true })) {
      const p = join(d, e.name);
      if (e.isDirectory()) walk(p);
      else out[relative(build, p)] = statSync(p).mtimeMs;
    }
  };
  walk(join(build, "lib", "clang"));
  for (const f of objectsAndConfigs()) out[relative(build, f)] = statSync(f).mtimeMs;
  return out;
}
function objectsAndConfigs(): string[] {
  const out: string[] = [];
  const walk = (d: string) => {
    for (const e of readdirSync(d, { withFileTypes: true })) {
      const p = join(d, e.name);
      if (e.isDirectory()) { if (!["CMakeFiles", ".semtest", "lib"].includes(e.name)) walk(p); }
      else if (e.name.endsWith(".cfg.py")) out.push(p);
    }
  };
  walk(join(build, "tools"));
  return out;
}

// ---- test sources ----------------------------------------------------------

// Every file under the suites being tested, so an edited test reruns and an
// edited shared input (Inputs/, lit.local.cfg) reruns everything.
function testFiles(): Record<string, number> {
  const out: Record<string, number> = {};
  const walk = (d: string) => {
    for (const e of readdirSync(d, { withFileTypes: true })) {
      const p = join(d, e.name);
      if (e.isDirectory()) walk(p);
      else out[p] = statSync(p).mtimeMs;
    }
  };
  for (const p of paths) statSync(p).isDirectory() ? walk(p) : (out[p] = statSync(p).mtimeMs);
  return out;
}

// lit names each test's profile after its path in the suite with "/" turned
// into "_"; map those names back to the test files lit discovered.
function discoverTests(): Map<string, string[]> {
  const r = spawnSync(join(build, "bin", "llvm-lit"), ["--show-tests", ...paths], { encoding: "utf8", maxBuffer: 1 << 28 });
  const byKey = new Map<string, string[]>();
  for (const line of r.stdout.split("\n")) {
    const m = /^\s+(.+?) :: (.+)$/.exec(line);
    if (!m) continue;
    const key = m[2].replaceAll("/", "_");
    (byKey.get(key) ?? byKey.set(key, []).get(key)!).push(m[2]);
  }
  return byKey;
}

// ---- coverage --------------------------------------------------------------

interface CoverageMap { names: bigint[]; tests: Record<string, number[] | "incomplete">; }
const MARKER = 0x73656d7465737421n;

function collect(raw: string, into: CoverageMap) {
  const index = new Map(into.names.map((n, i) => [n, i]));
  const sets = new Map<string, Set<number> | "incomplete">();
  for (const f of readdirSync(raw)) {
    const m = /^(.*)-\d+-[0-9a-f]*\.profraw$/.exec(f);
    if (!m) continue;
    const buf = readFileSync(join(raw, f));
    const prev = sets.get(m[1]);
    const n = buf.length / 8;
    if (prev === "incomplete") continue;
    if (n === 0 || buf.readBigUInt64LE((n - 1) * 8) !== MARKER) { sets.set(m[1], "incomplete"); continue; }
    const set = prev ?? new Set<number>();
    for (let i = 0; i < n - 1; i++) {
      const ref = buf.readBigUInt64LE(i * 8);
      let id = index.get(ref);
      if (id === undefined) { id = into.names.length; into.names.push(ref); index.set(ref, id); }
      set.add(id);
    }
    sets.set(m[1], set);
  }
  for (const [k, s] of sets) into.tests[k] = s === "incomplete" ? s : [...s].sort((a, b) => a - b);
  return sets.size;
}

const mapPath = () => join(state, "coverage.json");
function loadMap(): CoverageMap {
  if (!existsSync(mapPath())) return { names: [], tests: {} };
  const j = JSON.parse(readFileSync(mapPath(), "utf8"));
  return { names: j.names.map((s: string) => BigInt("0x" + s)), tests: j.tests };
}
function saveMap(m: CoverageMap) {
  writeFileSync(mapPath(), JSON.stringify({ names: m.names.map((n) => n.toString(16)), tests: m.tests }));
}

function runLit(tests: string[]) {
  const raw = join(state, "raw");
  rmSync(raw, { recursive: true, force: true });
  mkdirSync(raw, { recursive: true });
  const r = spawnSync(join(build, "bin", "llvm-lit"), ["-q", "--per-test-coverage", ...tests], {
    stdio: "inherit", env: { ...process.env, SEMTEST_DIR: raw },
  });
  return { raw, status: r.status ?? 1 };
}

function saveSnapshots() {
  mkdirSync(join(state, "objs"), { recursive: true });
  const t0 = performance.now();
  let fresh = 0;
  for (const obj of objects()) {
    const p = snapPath(obj), st = statSync(obj);
    if (existsSync(p)) {
      const old: ObjSnap = JSON.parse(readFileSync(p, "utf8"));
      if (old.mtime === st.mtimeMs && old.size === st.size) continue;
    }
    writeFileSync(p, JSON.stringify(snapshot(obj)));
    fresh++;
  }
  writeFileSync(join(state, "inputs.json"), JSON.stringify({ runtime: runtimeInputs(), tests: testFiles() }));
  log(`snapshotted ${fresh} object(s) in ${((performance.now() - t0) / 1000).toFixed(1)}s`);
}

// ---- select ----------------------------------------------------------------

type Affected = { all: string } | { names: Set<bigint>; why: string[] };

function affected(): Affected {
  const inputs = JSON.parse(readFileSync(join(state, "inputs.json"), "utf8"));
  const runtime = runtimeInputs();
  for (const [f, t] of Object.entries(runtime))
    if (inputs.runtime[f] !== t) return { all: `${f} changed` };
  for (const f of Object.keys(inputs.runtime)) if (!(f in runtime)) return { all: `${f} removed` };

  const names = new Set<bigint>(), why: string[] = [];
  for (const obj of objects()) {
    const p = snapPath(obj), st = statSync(obj);
    if (!existsSync(p)) return { all: `new object ${relative(build, obj)}` };
    const old: ObjSnap = JSON.parse(readFileSync(p, "utf8"));
    if (old.mtime === st.mtimeMs && old.size === st.size) continue;
    const buf = readFileSync(obj), atoms = readAtoms(buf), now = summarize(atoms);
    const counters = new Map<string, bigint>();
    for (const a of atoms) if (a.nameRef !== undefined) for (const r of a.refs) if (r.includes("___profc_")) counters.set(r, a.nameRef);
    const byName = new Map(atoms.map((a) => [(a.code ? "T " : "D ") + a.name, a]));
    for (const [k, h] of Object.entries(now)) {
      if (old.atoms[k] === h) continue;
      if (k === "<init>") return { all: `static initializers in ${relative(build, obj)}` };
      if (k.startsWith("D ")) return { all: `data ${k.slice(2)} in ${relative(build, obj)}` };
      const refs = byName.get(k)!.refs.filter((r) => counters.has(r));
      if (!refs.length) return { all: `uninstrumented ${k.slice(2)} in ${relative(build, obj)}` };
      for (const r of refs) names.add(counters.get(r)!);
      why.push(`${k.slice(2)} (${relative(build, obj)})`);
    }
    for (const k of Object.keys(old.atoms))
      if (!(k in now) && k.startsWith("D ")) return { all: `data ${k.slice(2)} removed from ${relative(build, obj)}` };
  }
  return { names, why };
}

if (mode === "record") {
  mkdirSync(state, { recursive: true });
  const { raw, status } = runLit(paths);
  const map: CoverageMap = { names: [], tests: {} };
  log(`recorded ${collect(raw, map)} test(s), ${map.names.length} distinct functions entered`);
  saveMap(map);
  saveSnapshots();
  process.exit(status);
} else if (mode === "select") {
  const t0 = performance.now();
  const a = affected();
  const map = loadMap();
  const tests = discoverTests();
  const oldInputs = JSON.parse(readFileSync(join(state, "inputs.json"), "utf8")).tests as Record<string, number>;
  const nowInputs = testFiles();
  const testPaths = new Set([...tests.values()].flat());
  const chosen = new Set<string>();
  let reason = "";
  if ("all" in a) reason = a.all;
  for (const [f, t] of Object.entries(nowInputs)) {
    if (oldInputs[f] === t) continue;
    const suiteRel = relative(suiteRoot, f);
    if (testPaths.has(suiteRel)) chosen.add(suiteRel);
    else reason ||= `${f} changed`;
  }
  const ids = "names" in a ? new Set(map.names.flatMap((n, i) => (a.names.has(n) ? [i] : []))) : new Set<number>();
  for (const [key, list] of tests) {
    const cov = map.tests[key];
    const pick = reason || cov === undefined || cov === "incomplete" || cov.some((i) => ids.has(i));
    if (pick) list.forEach((t) => chosen.add(t));
  }
  const total = testPaths.size;
  if (reason) log(`running all ${total} tests: ${reason}`);
  else log(`${chosen.size} of ${total} tests entered a changed function: ${(a as { why: string[] }).why.slice(0, 8).join(", ")}${(a as { why: string[] }).why.length > 8 ? ", ..." : ""} (${((performance.now() - t0) / 1000).toFixed(1)}s)`);
  if (!run) { for (const t of chosen) console.log(t); process.exit(0); }
  const selected = [...chosen].map((t) => join(suiteRoot, t));
  if (!selected.length) { log("nothing to run"); saveSnapshots(); process.exit(0); }
  const { raw, status } = runLit(selected);
  collect(raw, map);
  saveMap(map);
  saveSnapshots();
  process.exit(status);
} else {
  console.error("usage: semtest.ts record|select -C <build> [--run] <lit test paths...>");
  process.exit(2);
}
