// semninja: run ninja, skipping the recompiles a header edit cannot affect.
//
// Works on a build configured with `-Xclang -fsemantic-deps`, which makes
// clang write <obj>.sdeps next to each object. For every header whose contents
// changed since the last successful run, clang-semindex lists the names the
// edit touched; a translation unit that is out of date only because of such
// headers (and the PCHs built from them), and that never used any of those
// names, keeps its object: its mtime is bumped so ninja treats it as current.
// Anything else is left to ninja.
//
// With --verify, every kept object is then recompiled and compared byte for
// byte; a mismatch is reported and the fresh object replaces the kept one.
//
// --verify-last does the same for the objects the previous run kept.
//
// usage: bun semninja.ts -C <builddir> [--indexer <clang-semindex>] [--verify]
//                        [--verify-last] [--dry-run] [--no-build] [targets...]
import { createHash } from "crypto";
import { execFileSync, spawnSync } from "child_process";
import {
  existsSync, mkdirSync, readFileSync, realpathSync, statSync, utimesSync, writeFileSync,
} from "fs";
import { basename, dirname, join, resolve } from "path";
import { readDepsLog, refreshBuildLog, refreshDeps } from "./depslog.ts";
import { semdiff, type Idx } from "./semdiff.ts";

const args = process.argv.slice(2);
let dir = ".", indexer = "", dryRun = false, noBuild = false, verify = false, verifyLast = false;
const targets: string[] = [];
for (let i = 0; i < args.length; i++) {
  if (args[i] === "-C") dir = args[++i];
  else if (args[i] === "--indexer") indexer = args[++i];
  else if (args[i] === "--dry-run") dryRun = true;
  else if (args[i] === "--no-build") noBuild = true;
  else if (args[i] === "--verify") verify = true;
  else if (args[i] === "--verify-last") verifyLast = true;
  else targets.push(args[i]);
}
dir = resolve(dir);
indexer ||= join(dirname(realpathSync(process.execPath)), "clang-semindex");
const stateDir = join(dir, ".semninja");
const blobs = join(stateDir, "blobs");
const snapshotFile = join(stateDir, "headers.json");
const t0 = performance.now();
const log = (s: string) => console.error(`semninja: ${s}`);
const ms = () => `${((performance.now() - t0) / 1000).toFixed(1)}s`;
const sha = (b: Buffer | string) => createHash("sha256").update(b).digest("hex");

function ninja(extra: string[]): number {
  return spawnSync("ninja", ["-C", dir, ...extra], { stdio: "inherit" }).status ?? 1;
}

// Dependency graph from ninja's deps log: object -> inputs, with headers
// reached through a PCH folded in.
type Graph = { deps: Map<string, string[]>; pchHeaders: Map<string, Set<string>> };
function readGraph(): Graph {
  // Straight from the deps log: several times faster than `ninja -t deps`.
  const log = readDepsLog(join(dir, ".ninja_deps"));
  const names: string[] = [];
  for (const [p, id] of log.ids) names[id] = p;
  const abs = (p: string) => (p.startsWith("/") ? p : join(dir, p));
  const deps = new Map<string, string[]>();
  for (const [out, inputs] of log.deps)
    // Outputs no longer in the build linger in the log; skip what is gone.
    if (existsSync(abs(names[out]))) deps.set(names[out], inputs.map((i) => abs(names[i])));
  const pchHeaders = new Map<string, Set<string>>();
  for (const [o, d] of deps) if (o.endsWith(".pch")) pchHeaders.set(join(dir, o), new Set(d));
  return { deps, pchHeaders };
}

const headersOf = (g: Graph) => {
  const hs = new Set<string>();
  for (const d of g.deps.values()) for (const p of d) if (/\.(h|hpp|hh|inc|def)$/.test(p)) hs.add(p);
  return hs;
};

function snapshot(g: Graph, only?: Set<string>) {
  mkdirSync(blobs, { recursive: true });
  const prev: Record<string, string> = existsSync(snapshotFile) ? JSON.parse(readFileSync(snapshotFile, "utf8")) : {};
  for (const h of only ?? headersOf(g)) {
    if (!existsSync(h)) { delete prev[h]; continue; }
    const data = readFileSync(h);
    const hash = sha(data);
    if (!existsSync(join(blobs, hash))) writeFileSync(join(blobs, hash), data);
    prev[h] = hash;
  }
  writeFileSync(snapshotFile, JSON.stringify(prev));
}

// Objects share most of their headers, so remember each stat.
const mtimes = new Map<string, number>();
const mtime = (p: string) => {
  let t = mtimes.get(p);
  if (t === undefined) {
    try { t = statSync(p).mtimeMs; } catch { t = Infinity; }
    mtimes.set(p, t);
  }
  return t;
};

/// Recompiles each kept object to a temporary file and compares it with the
/// kept one. A mismatch means a dependency was missed: the fresh object takes
/// the kept one's place and the miss is reported.
async function verifyKept(objs: string[]): Promise<boolean> {
  const compdb = JSON.parse(execFileSync("ninja", ["-C", dir, "-t", "compdb"], { maxBuffer: 1 << 30 }).toString()) as
    { output: string; command: string }[];
  const cmds = new Map(compdb.map((c) => [c.output, c.command]));
  const tmp = join(stateDir, "verify");
  mkdirSync(tmp, { recursive: true });
  const jobs = navigator.hardwareConcurrency;
  let next = 0;
  const misses: string[] = [];
  await Promise.all(Array.from({ length: jobs }, async (_, w) => {
    while (next < objs.length) {
      const o = objs[next++];
      const out = join(tmp, `${w}.o`);
      // Same command, minus the depfile and the codegen cache (a cache hit
      // would compare the kept object with itself), with its own output.
      const cmd = cmds.get(o)!.replace(/ -MD -MT \S+ -MF \S+/, "")
        .replace(/ -Xclang -fcodegen-cache-path=\S+/, "").replace(/ -o \S+/, ` -o ${out}`);
      const p = Bun.spawn(["sh", "-c", cmd], { cwd: dir, stdout: "ignore", stderr: "ignore" });
      if ((await p.exited) !== 0 || !readFileSync(out).equals(readFileSync(join(dir, o)))) {
        misses.push(o);
        if (existsSync(out)) writeFileSync(join(dir, o), readFileSync(out));
      }
    }
  }));
  log(`verify: ${objs.length - misses.length}/${objs.length} kept objects identical to a fresh compile`);
  for (const m of misses) log(`verify: MISMATCH ${m} (replaced with the fresh object)`);
  return misses.length === 0;
}

async function main(): Promise<number> {
  if (verifyLast) {
    const list = join(stateDir, "last-skipped.txt");
    const objs = existsSync(list) ? readFileSync(list, "utf8").split("\n").filter(Boolean) : [];
    return (await verifyKept(objs)) ? 0 : 1;
  }
  if (!existsSync(snapshotFile)) {
    log("no snapshot yet; running ninja and recording header contents");
    const rc = ninja(targets);
    if (rc === 0) snapshot(readGraph());
    return rc;
  }
  const g = readGraph();
  if (process.env.SEMNINJA_TIMING) log(`read the dependency graph (${ms()})`);
  const snap: Record<string, string> = JSON.parse(readFileSync(snapshotFile, "utf8"));
  const changed = Object.keys(snap).filter((h) => !existsSync(h) || sha(readFileSync(h)) !== snap[h]);
  log(`${changed.length} header(s) changed since the last build (${ms()})`);
  if (!changed.length) return ninja(targets);

  // What the edits touched, per header. Any header we cannot analyze makes
  // every one of its dependents a recompile, exactly as with plain ninja.
  const names = new Set<string>(), bodies = new Set<string>();
  const analyzed = new Set<string>();
  // Only dependents of an analyzed header can be skipped.
  const candidates = new Set<string>();
  const compdb = JSON.parse(readFileSync(join(dir, "compile_commands.json"), "utf8")) as
    { file: string; output: string; command: string }[];
  const sourceOf = new Map(compdb.map((c) => [c.output.replace(dir + "/", ""), c.file]));
  const commandOf = new Map(compdb.map((c) => [c.file, c.command]));
  const indexCache = join(stateDir, "index");
  mkdirSync(indexCache, { recursive: true });
  const st = statSync(indexer);
  const indexerId = `${st.size}:${st.mtimeMs}`;
  // Header -> objects that include it, directly or through a PCH.
  const objectsOf = new Map<string, Set<string>>();
  const note = (h: string, o: string) => {
    if (!objectsOf.has(h)) objectsOf.set(h, new Set());
    objectsOf.get(h)!.add(o);
  };
  for (const [o, d] of g.deps) {
    if (!o.endsWith(".o")) continue;
    for (const x of d) {
      note(x, o);
      g.pchHeaders.get(x)?.forEach((h) => note(h, o));
    }
  }
  const dependentsOf = (h: string) => [...(objectsOf.get(h) ?? [])];
  // Headers are analyzed in parallel; each runs the indexer twice.
  const analyze = async (h: string) => {
    if (!existsSync(h)) { log(`${h} was deleted; not analyzed`); return; }
    // Any dependent lends its compile command; Foo.cpp for Foo.h is the one
    // most likely to include it directly, should it need to (see --in-source).
    const sources = dependentsOf(h).map((o) => sourceOf.get(o)).filter((s): s is string => !!s?.endsWith(".cpp"));
    const stem = basename(h).replace(/\.\w+$/, "");
    const tu = sources.find((s) => basename(s, ".cpp") === stem) ?? sources[0];
    if (!tu) return;
    // An index depends only on the header's bytes, how it is parsed, the
    // command lent by the source and the indexer, so it is cached on those:
    // the index made after this build is the "before" side of the next one.
    const index = async (mode: string[], contents?: string): Promise<Idx | null> => {
      const bytes = readFileSync(contents ?? h);
      const key = sha(Buffer.concat([bytes, Buffer.from(`\0${mode.join(" ")}\0${h}\0${tu}\0${commandOf.get(tu)}\0${indexerId}`)]));
      const cached = join(indexCache, `${key}.json`);
      if (existsSync(cached)) return JSON.parse(readFileSync(cached, "utf8"));
      const p = Bun.spawn([indexer, "-p", dir, "--header", h, ...mode, ...(contents ? ["--contents", contents] : []), tu],
        { stdout: "pipe", stderr: "ignore" });
      const out = await new Response(p.stdout).text();
      if ((await p.exited) !== 0) return null;
      writeFileSync(cached, out);
      return JSON.parse(out);
    };
    const both = (mode: string[]) => Promise.all([index(mode), index(mode, join(blobs, snap[h]))]);
    // A header is indexed by itself unless one version does not compile that
    // way; then both are indexed as the source includes them.
    let [now, before] = await both([]);
    if (!now?.found || now.errors || !before?.found || before.errors) [now, before] = await both(["--in-source"]);
    if (!now || !before) { log(`could not index ${h}; not analyzed`); return; }
    const r = semdiff(before, now);
    if (r.all) { log(`${h}: ${r.all}; not analyzed`); return; }
    r.names.forEach((n) => names.add(n));
    r.bodies.forEach((n) => bodies.add(n + "()"));
    analyzed.add(realpathSync(h));
    dependentsOf(h).forEach((o) => candidates.add(o));
  };
  let nextHeader = 0;
  const workers = Math.max(1, Math.floor(navigator.hardwareConcurrency / 2));
  await Promise.all(Array.from({ length: workers }, async () => {
    while (nextHeader < changed.length) await analyze(changed[nextHeader++]);
  }));
  log(`analyzed ${analyzed.size}/${changed.length}: ${names.size} declaration(s), ${bodies.size} body(ies) changed (${ms()})`);

  // PCHs and generated files first, so that everything a skipped object
  // depends on is already up to date when its mtime is bumped.
  const pchs = [...g.pchHeaders.keys()].map((p) => p.replace(dir + "/", ""));
  if (!dryRun && pchs.length && ninja(pchs) !== 0) return 1;

  let skipped = 0, considered = 0;
  const skippedList: string[] = [];
  const reasons = new Map<string, number>();
  for (const o of candidates) {
    const d = g.deps.get(o)!;
    const obj = join(dir, o), t = mtime(obj);
    const stale = d.filter((p) => mtime(p) > t);
    if (!stale.length) continue;
    // Only skip when every newer input is an analyzed header or a PCH.
    if (!stale.every((p) => existsSync(p) && (analyzed.has(realpathSync(p)) || g.pchHeaders.has(p)))) continue;
    considered++;
    const sd = obj + ".sdeps";
    let reason = "";
    if (!existsSync(sd)) reason = "no .sdeps";
    else {
      const lines = readFileSync(sd, "utf8").split("\n");
      if (lines.includes("*")) reason = "depended on everything";
      else if ([...analyzed].some((h) => lines.includes(`#file ${h}`))) reason = "parses the header as text";
      else if (lines.some((n) => names.has(n) || bodies.has(n))) reason = "uses what changed";
    }
    if (reason) { reasons.set(reason, (reasons.get(reason) ?? 0) + 1); continue; }
    skipped++;
    skippedList.push(o);
    if (!dryRun) { const now = new Date(); utimesSync(obj, now, now); }
  }
  log(`skipping ${skipped} of ${considered} recompile(s) caused by the edit; ` +
    [...reasons].map(([r, n]) => `${n} ${r}`).join(", ") + ` (${ms()})`);
  writeFileSync(join(stateDir, "last-skipped.txt"), skippedList.join("\n") + "\n");
  if (dryRun) return 0;
  // A bumped mtime alone makes ninja rebuild: ninja checks the mtimes it
  // recorded in its deps and build logs, so record the new ones there.
  if (skippedList.length) {
    refreshDeps(readDepsLog(join(dir, ".ninja_deps")), skippedList, dir);
    refreshBuildLog(dir, skippedList);
  }
  if (noBuild) return 0;
  const rc = ninja(targets);
  if (rc === 0) snapshot(g, new Set(changed));
  log(`done (${ms()})`);
  if (rc === 0 && verify && skippedList.length) return (await verifyKept(skippedList)) ? 0 : 1;
  return rc;
}

process.exit(await main());
