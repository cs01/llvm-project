// project: -fcir-modernize across a whole compilation database, so functions
// declared in headers and called from other translation units can change too.
//
//  1. propose: every TU exports the candidates it defines and proves
//     (-fcir-modernize-export). Policy drops candidates whose declarations
//     are outside --root or in a public header directory.
//  2. plan:    every TU is compiled again with the proposals
//     (-fcir-modernize-plan) and exports the edits its own declarations and
//     calls need, or why it blocks a function.
//  3. merge:   a function survives if its defining TU still proves it, no TU
//     blocks it, and everything it forwards to (or is forwarded from)
//     survives. A TU that CIR cannot compile blocks every planned function
//     whose name appears in its preprocessed text.
//  4. prove:   every TU that saw a surviving edit (and every TU CIR could not
//     compile) is compiled with all edits mounted, and its IR must be
//     unchanged (lib.ts). --apply writes the edits only if every TU passes.
//
// The compilation database is taken to be the whole program: a caller outside
// it is not seen. Public header directories exist so a library's API is only
// changed on purpose (--allow-public-api).
//
// usage: bun project.ts --clang <clang> -p <compile_commands.json> [--root <dir>]
//                       [--public-headers <dir>]... [--allow-public-api]
//                       [--only <rewrites>] [--skip <rewrites>] [--inventory <file>]
//                       [--lifetime-safety]
//                       [--apply] [-j N] [-v] [files or globs...]
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "fs";
import { tmpdir } from "os";
import { join, resolve, sep } from "path";
import {
  type Cmd, type Edit, applyEdits, byFile, defaultJobs, firstError, loadCommands, overlay, pool, prove, run,
} from "./lib.ts";

type Candidate = {
  usr: string;
  index: number;
  kind: string;
  function: string;
  param: string;
  defined: boolean;
  location: string;
  blocked?: string;
  internal?: boolean;
  method?: boolean;
  changesType?: boolean;
  refines?: boolean;
  edits?: Edit[];
  needs?: { usr: string; index: number; kind: string }[];
};
type Site = {
  file: string; offset: number; line: number; category: string; row: string;
  state: "proven" | "covered" | "outside" | "unknown"; why?: string; usr?: string; index?: number;
};
// facts is false when CIRGen failed and only the AST half ran.
type Export = { facts?: boolean; candidates: Candidate[]; sites?: Site[]; skippedNames?: string[]; cFiles?: string[] };

const argv = process.argv.slice(2);
let clang = "", compdb = "", root = process.cwd(), jobs = defaultJobs;
let allowPublic = false, apply = false, verbose = false, inventory = "", lifetimeSafety = false;
const select: string[] = [];
const publicDirs: string[] = [], patterns: string[] = [];
for (let i = 0; i < argv.length; i++) {
  const a = argv[i];
  if (a === "--clang") clang = resolve(argv[++i]);
  else if (a === "-p") compdb = resolve(argv[++i]);
  else if (a === "--root") root = resolve(argv[++i]);
  else if (a === "--public-headers") publicDirs.push(resolve(argv[++i]));
  else if (a === "--allow-public-api") allowPublic = true;
  else if (a === "--apply") apply = true;
  else if (a === "-j") jobs = +argv[++i];
  else if (a === "-v") verbose = true;
  else if (a === "--inventory") inventory = resolve(argv[++i]);
  else if (a === "--lifetime-safety") lifetimeSafety = true;
  else if (a === "--only" || a === "--skip") select.push(`-fcir-modernize-${a.slice(2)}=${argv[++i]}`);
  else if (a === "--view") select.push(`-fcir-modernize-view=${argv[++i]}`);
  else patterns.push(a);
}
if (!clang || !compdb) {
  console.error("usage: bun project.ts --clang <clang> -p <compile_commands.json> [options] [files...]");
  process.exit(2);
}

const keyOf = (c: { usr: string; index: number; kind: string }) => `${c.kind}#${c.index}#${c.usr}`;
const under = (file: string, dir: string) => file === dir || file.startsWith(dir + sep);
const scratch = mkdtempSync(join(tmpdir(), "cirmodernize-project-"));
const cmds = loadCommands(compdb, patterns);
// A run on some of the project's TUs also takes every other TU that includes
// a file under --root, since those see the declarations an edit changes. What
// -M cannot see is a TU declaring a function itself; see the check after the
// plan.
if (patterns.length) {
  const picked = new Set(cmds.map((c) => c.file));
  const rest = loadCommands(compdb, []).filter((c) => !picked.has(c.file));
  const seen = await pool(rest, jobs, async (c) => {
    const r = await run(clang, [...c.args, "-M", "-o", "-", c.file], c.dir);
    // A TU that cannot be scanned might include anything.
    if (r.code !== 0) return true;
    return r.out.split(/\s+/).some((d) => d && d !== "\\" && !d.endsWith(":") && under(resolve(c.dir, d), root));
  });
  const added = rest.filter((_, i) => seen[i]);
  cmds.push(...added);
  console.log(`${picked.size} TUs picked, ${added.length} more include a file under --root, ${rest.length - added.length} do not`);
}

async function exportTU(c: Cmd, out: string, plan?: string): Promise<Export | string> {
  const r = await run(clang, [
    ...c.args, "-fclangir", "-S", "-emit-cir", "-fcir-modernize", "-fno-color-diagnostics",
    "-fno-crash-diagnostics", ...select, `-fcir-modernize-export=${out}`, ...(plan ? [`-fcir-modernize-plan=${plan}`] : []),
    "-o", "/dev/null", c.file,
  ], c.dir);
  // A translation unit CIR cannot compile still writes an export from the AST
  // (or from before the CIR pass that failed); only one without an export
  // is unknown.
  if (!existsSync(out)) return firstError(r.err, r.code);
  // A crash partway through the export leaves it truncated.
  let e: Export;
  try { e = JSON.parse(readFileSync(out, "utf8")); }
  catch { return `truncated export: ${firstError(r.err, r.code)}`; }
  if (r.code !== 0 || e.facts === false) astOnly.add(c.file);
  return e;
}

function policy(c: Candidate): string | undefined {
  for (const e of c.edits ?? []) {
    if (!under(e.file, root)) return "declared outside --root";
    if (!allowPublic && publicDirs.some((d) => under(e.file, d))) return "declared in a public header";
  }
  return undefined;
}

// 1. propose
const t0 = performance.now();
const skipped = new Map<string, string>();
const astOnly = new Set<string>();
const pass1 = await pool(cmds.map((c, i) => [c, i] as const), jobs,
                         ([c, i]) => exportTU(c, join(scratch, `p1-${i}.json`)));
const proposals = new Map<string, Candidate>();
const blockedBy = new Map<string, number>();
const block = (why: string) => blockedBy.set(why, (blockedBy.get(why) ?? 0) + 1);
const seen1 = new Set<string>();
// Why a defining TU did not propose a candidate, for the inventory.
const why1 = new Map<string, string>();
// A header's sites come from every TU that includes it; one copy each.
const sites = new Map<string, Site>();
// Identifiers in #if branches some TU's build skips: a call there gets no
// edit, so the other configuration would stop compiling.
const skippedNames = new Set<string>();
// Files some C TU includes.
const cFiles = new Set<string>();
pass1.forEach((e, i) => {
  if (typeof e === "string") return void skipped.set(cmds[i].file, e);
  for (const n of e.skippedNames ?? []) skippedNames.add(n);
  for (const f of e.cFiles ?? []) cFiles.add(f);
  for (const s of e.sites ?? []) {
    if (!under(s.file, root)) continue;
    if (e.facts === false && s.index !== undefined) s.state = "unknown";
    sites.set(`${s.file}:${s.offset}:${s.category}`, s);
  }
  for (const c of e.candidates) {
    if (!c.defined || proposals.has(keyOf(c)) || seen1.has(keyOf(c))) continue;
    seen1.add(keyOf(c));
    if (c.blocked) {
      block(c.blocked);
      why1.set(keyOf(c), c.blocked);
      continue;
    }
    const why = policy(c);
    if (why) why1.set(keyOf(c), why);
    if (why) block(why);
    else proposals.set(keyOf(c), c);
  }
});
for (const [k, c] of proposals) {
  if (c.changesType === false) continue;
  const name = c.function.split("::").pop()!;
  if (skippedNames.has(name) || (name.startsWith("operator") && skippedNames.size)) {
    proposals.delete(k);
    why1.set(k, "named in an #if branch some TU skips");
    block("named in an #if branch some TU skips");
  }
}
const planPath = join(scratch, "plan.json");
writeFileSync(planPath, JSON.stringify({
  functions: [...proposals.values()].map(({ usr, index, kind }) => ({ usr, index, kind })),
}));

// 2. plan
const pass2 = proposals.size
  ? await pool(cmds.map((c, i) => [c, i] as const), jobs,
               ([c, i]) => exportTU(c, join(scratch, `p2-${i}.json`), planPath))
  : [];

// 3. merge
const edits = new Map<string, Edit[]>(), needs = new Map<string, Set<string>>();
const provenHere = new Set<string>(), blockers = new Map<string, string>();
const tuKeys: Set<string>[] = cmds.map(() => new Set());
pass2.forEach((e, i) => {
  if (typeof e === "string") return void skipped.set(cmds[i].file, e);
  for (const c of e.candidates) {
    const k = keyOf(c);
    if (!proposals.has(k)) continue;
    if (c.blocked) {
      if (!blockers.has(k)) blockers.set(k, c.blocked);
      continue;
    }
    const why = policy(c);
    if (why && !blockers.has(k)) blockers.set(k, why);
    if (c.defined) provenHere.add(k);
    edits.set(k, [...(edits.get(k) ?? []), ...(c.edits ?? [])]);
    const n = needs.get(k) ?? new Set<string>();
    for (const d of c.needs ?? []) n.add(keyOf(d));
    needs.set(k, n);
    tuKeys[i].add(k);
  }
});

// These wrote no export at all, so their calls are unknown. Any planned
// function they might name is blocked; the preprocessed text is searched so a
// call written through a macro counts too.
if (skipped.size && proposals.size) {
  const names = new Map<string, string[]>();
  for (const [k, c] of proposals) {
    const short = c.function.split("::").pop()!;
    names.set(short, [...(names.get(short) ?? []), k]);
  }
  await pool(cmds.filter((c) => skipped.has(c.file)), jobs, async (c) => {
    const r = await run(clang, [...c.args, "-E", "-P", "-o", "-", c.file], c.dir);
    const words = new Set(r.code === 0 ? r.out.match(/[A-Za-z_]\w*/g) ?? [] : []);
    for (const [name, ks] of names)
      if (r.code !== 0 || words.has(name))
        for (const k of ks) if (!blockers.has(k)) blockers.set(k, "named in a TU CIR could not compile");
  });
}

// const and nullability are C too; the rest are C++ syntax.
const inC = new Set(["const", "nonnull", "nullable"]);
for (const [k, es] of edits)
  if (!inC.has(proposals.get(k)!.kind) && es.some((e) => cFiles.has(e.file)) && !blockers.has(k))
    blockers.set(k, "C++ syntax in a file a C TU includes");
const live = new Set([...proposals.keys()].filter((k) => provenHere.has(k) && !blockers.has(k)));
for (const k of proposals.keys())
  if (!live.has(k) && !blockers.has(k)) blockers.set(k, "no longer proven by its defining TU");
// A parameter that became a reference needs no nullability qualifier, and
// the two edit the same declarator.
for (const k of live) {
  const c = proposals.get(k)!;
  if ((c.kind === "nonnull" || c.kind === "nullable") && live.has(keyOf({ ...c, kind: "ref" }))) {
    live.delete(k);
    blockers.set(k, "nullability: became a reference");
  }
}
// The TUs left out of a partial run include nothing under --root, but one
// can still declare an external free function itself and call it. A method
// cannot be declared outside its class, which a header under --root holds.
if (patterns.length)
  for (const k of live) {
    const c = proposals.get(k)!;
    if (c.internal || c.method) continue;
    live.delete(k);
    blockers.set(k, "external free function in a partial run");
  }
function settle() {
  for (let changed = true; changed;) {
    changed = false;
    for (const k of live)
      if ([...(needs.get(k) ?? [])].some((d) => !live.has(d))) {
        live.delete(k);
        blockers.set(k, "forwarded to or from a parameter that stays a pointer");
        changed = true;
      }
  }
}
settle();

// Rewrites that change the code are proven by equality after normalization,
// which then covers every rewrite in the run.
const refine = [...live].some((k) => proposals.get(k)?.refines);
if (refine) console.log("proving by equality after normalization (cir-modernize-ircmp --refine)");

// 4. prove. A TU that fails blocks the rewrites its edits belong to, and the
// rest are proven again, so one bad edit does not hold back the others. A
// failing TU with no rewrite of its own to block (one CIR could not compile)
// stops the run instead.
let all: Edit[] = [], toProve: Cmd[] = [];
let failed = 0, proven = 0;
const findings = new Set<string>();
for (let round = 1; ; round++) {
  all = [...live].flatMap((k) => edits.get(k)!);
  const picked = cmds.map((c, i) => [c, i] as const)
    .filter(([c, i]) => skipped.has(c.file) || astOnly.has(c.file) || [...tuKeys[i]].some((k) => live.has(k)));
  toProve = picked.map(([c]) => c);
  failed = proven = 0;
  if (!all.length) break;
  const yaml = overlay(mkdtempSync(join(scratch, "overlay-")), all);
  const bad: number[] = [];
  await pool(picked, jobs, async ([c, i]) => {
    const p = await prove(clang, c, yaml, lifetimeSafety ? ["-Wlifetime-safety"] : [], refine);
    if (p.status === "ok") proven++;
    // A TU that includes an edited file but cannot be compiled as is (often a
    // generated header the build has not made yet) proves nothing, so it
    // counts against the rewrites like a failure.
    if (p.status !== "ok") {
      failed++;
      bad.push(i);
      // A right lifetime annotation can make a caller warn: that is a
      // dangling use it exposed, not a broken rewrite.
      const w = p.detail?.match(/^new warning: (.*\[-W(dangling[\w-]*|return-stack-address|lifetime-safety[\w-]*)\])$/);
      if (w) findings.add(w[1]);
      console.log(`${w ? "FINDING" : "FAIL"} ${c.file}\n  ${p.detail}`);
    }
  });
  let culprits = bad.flatMap((i) => [...tuKeys[i]].filter((k) => live.has(k)));
  if (!failed || !culprits.length || round === 5) break;
  // A failing TU with several rewrites: find the ones that make it fail by
  // proving it with halves of them (the others held back), so one bad
  // rewrite does not take the rest of the TU with it. A set that fails while
  // neither half does is kept whole.
  const proves = async (i: number, keep: string[], mine: string[]) => {
    const held = new Set(mine.filter((k) => !keep.includes(k)));
    const es = [...live].filter((k) => !held.has(k)).flatMap((k) => edits.get(k)!);
    const p = await prove(clang, cmds[i], overlay(mkdtempSync(join(scratch, "split-")), es),
                          lifetimeSafety ? ["-Wlifetime-safety"] : [], refine);
    return p.status === "ok";
  };
  const guiltyIn = async (i: number, set: string[], mine: string[], failing: boolean): Promise<string[]> => {
    if (!failing && (await proves(i, set, mine))) return [];
    if (set.length === 1) return set;
    const a = set.slice(0, set.length >> 1), b = set.slice(set.length >> 1);
    const found = [...await guiltyIn(i, a, mine, false), ...await guiltyIn(i, b, mine, false)];
    return found.length ? found : set;
  };
  const guilty = (await pool(bad, jobs, async (i) => {
    const mine = [...tuKeys[i]].filter((k) => live.has(k));
    return mine.length > 1 ? guiltyIn(i, mine, mine, true) : mine;
  })).flat();
  if (guilty.length) culprits = guilty;
  for (const k of culprits) {
    live.delete(k);
    blockers.set(k, "a TU it edits did not prove unchanged");
  }
  settle();
  console.log(`round ${round}: ${failed} TUs failed, ${culprits.length} rewrites blocked, proving the rest again`);
}
for (const k of proposals.keys()) if (!live.has(k)) block(blockers.get(k)!);

const secs = ((performance.now() - t0) / 1000).toFixed(1);
console.log(`${cmds.length} TUs (${astOnly.size} CIR could not fully compile, ${skipped.size} without an export), ${secs}s`);
{
  const why = new Map<string, number>();
  for (const w of skipped.values()) why.set(w, (why.get(w) ?? 0) + 1);
  for (const [w, n] of [...why].sort((x, y) => y[1] - x[1]).slice(0, 5)) console.log(`  ${n} without an export: ${w}`);
}
console.log(`${proposals.size} parameters proposed, ${live.size} rewritten, ` +
            `${proposals.size - live.size} blocked`);
console.log(`${proven} TUs proven unchanged, ${failed} failed`);
if (findings.size) {
  console.log(`${findings.size} dangling uses exposed by the rewrites (each blocks its rewrite):`);
  for (const f of findings) console.log(`  ${f}`);
}
if (blockedBy.size) {
  console.log("not rewritten:");
  for (const [why, n] of [...blockedBy].sort((x, y) => y[1] - x[1])) console.log(`  ${String(n).padStart(6)}  ${why}`);
}
if (verbose)
  for (const k of live) {
    const c = proposals.get(k)!;
    console.log(`  ${c.location}: ${c.kind} ${c.function}(${c.param})`);
  }

if (inventory) writeInventory();

// A pointer parameter is proven when it became a reference; a candidate that
// was blocked is outside for that reason; one with no candidate is not
// dereferenced on every path, so null is a value it may legitimately take.
// A reference member set from a parameter is covered once that parameter is
// lifetimebound.
function writeInventory() {
  for (const s of sites.values()) {
    if (s.index === undefined || s.state === "unknown" && astOnly.has(s.file)) continue;
    if (s.row === "lifetime") {
      const k = keyOf({kind : "capture-by", index : s.index, usr : s.usr!});
      if (live.has(k))
        [s.state, s.why] = [
          "covered", "rewritten to lifetimebound: clang checks the callers"
        ];
      else if (blockers.has(k) || why1.has(k))
        s.why = blockers.get(k) ?? why1.get(k);
      continue;
    }
    const k = keyOf({ kind: "ref", index: s.index, usr: s.usr! });
    if (live.has(k)) [s.state, s.why] = ["proven", "rewritten to a reference"];
    else if (blockers.has(k) || why1.has(k)) [s.state, s.why] = ["outside", blockers.get(k) ?? why1.get(k)];
    else [s.state, s.why] = ["outside", "may be null: not dereferenced on every path"];
  }
  const all = [...sites.values()];
  const tally = (key: (s: Site) => string) => {
    const t = new Map<string, Record<string, number>>();
    for (const s of all) {
      const r = t.get(key(s)) ?? { total: 0, proven: 0, covered: 0, outside: 0, unknown: 0 };
      r.total++, r[s.state]++;
      t.set(key(s), r);
    }
    return Object.fromEntries([...t].sort((a, b) => b[1].total - a[1].total));
  };
  const rows = tally((s) => s.row), categories = tally((s) => s.category);
  const reasons = tally((s) => `${s.category}: ${s.state}${s.why ? ` (${s.why})` : ""}`);
  const proven = all.filter((s) => s.state === "proven").length;
  writeFileSync(inventory, JSON.stringify({ root, sites: all.length, proven, rows, categories, reasons, list: all }, null, 1));
  console.log(`inventory: ${all.length} unsafe sites, ${proven} proven (${(100 * proven / Math.max(1, all.length)).toFixed(1)}%), written to ${inventory}`);
  for (const [row, r] of Object.entries(rows))
    console.log(`  ${row.padEnd(8)} ${String(r.total).padStart(6)} sites  ${r.proven} proven  ${r.covered} covered  ${r.outside} outside  ${r.unknown} unknown`);
}

if (apply && all.length) {
  if (failed) {
    console.log("not applying: some TUs did not prove unchanged");
  } else {
    for (const [file, es] of byFile(all)) writeFileSync(file, applyEdits(readFileSync(file), es));
    console.log(`applied edits to ${byFile(all).size} files`);
  }
}
if (!verbose) rmSync(scratch, { recursive: true, force: true });
process.exit(failed ? 1 : 0);
