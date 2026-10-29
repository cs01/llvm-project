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
//                       [--apply] [-j N] [-v] [files or globs...]
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "fs";
import { tmpdir } from "os";
import { join, resolve, sep } from "path";
import {
  type Cmd, type Edit, applyEdits, byFile, firstError, loadCommands, overlay, pool, prove, run,
} from "./lib.ts";

type Candidate = {
  usr: string;
  index: number;
  kind: "ref" | "const";
  function: string;
  param: string;
  defined: boolean;
  location: string;
  blocked?: string;
  edits?: Edit[];
  needs?: { usr: string; index: number; kind: "ref" | "const" }[];
};
// facts is false when CIRGen failed and only the AST half ran.
type Export = { facts?: boolean; candidates: Candidate[] };

const argv = process.argv.slice(2);
let clang = "", compdb = "", root = process.cwd(), jobs = navigator.hardwareConcurrency;
let allowPublic = false, apply = false, verbose = false;
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

async function exportTU(c: Cmd, out: string, plan?: string): Promise<Export | string> {
  const r = await run(clang, [
    ...c.args, "-fclangir", "-S", "-emit-cir", "-fcir-modernize", "-fno-color-diagnostics",
    `-fcir-modernize-export=${out}`, ...(plan ? [`-fcir-modernize-plan=${plan}`] : []),
    "-o", "/dev/null", c.file,
  ], c.dir);
  // A translation unit CIR cannot compile still writes an export from the AST
  // (or from before the CIR pass that failed); only one without an export
  // is unknown.
  if (!existsSync(out)) return firstError(r.err, r.code);
  const e = JSON.parse(readFileSync(out, "utf8")) as Export;
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
pass1.forEach((e, i) => {
  if (typeof e === "string") return void skipped.set(cmds[i].file, e);
  for (const c of e.candidates) {
    if (!c.defined || proposals.has(keyOf(c)) || seen1.has(keyOf(c))) continue;
    seen1.add(keyOf(c));
    if (c.blocked) {
      block(c.blocked);
      continue;
    }
    const why = policy(c);
    if (why) block(why);
    else proposals.set(keyOf(c), c);
  }
});
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

const live = new Set([...proposals.keys()].filter((k) => provenHere.has(k) && !blockers.has(k)));
for (const k of proposals.keys())
  if (!live.has(k) && !blockers.has(k)) blockers.set(k, "no longer proven by its defining TU");
for (let changed = true; changed;) {
  changed = false;
  for (const k of live)
    if ([...(needs.get(k) ?? [])].some((d) => !live.has(d))) {
      live.delete(k);
      blockers.set(k, "forwarded to or from a parameter that stays a pointer");
      changed = true;
    }
}
for (const k of proposals.keys()) if (!live.has(k)) block(blockers.get(k)!);

// 4. prove
const all = [...live].flatMap((k) => edits.get(k)!);
const toProve = cmds.filter((c, i) => skipped.has(c.file) || astOnly.has(c.file) || [...tuKeys[i]].some((k) => live.has(k)));
let failed = 0, proven = 0;
if (all.length) {
  const yaml = overlay(mkdtempSync(join(scratch, "overlay-")), all);
  await pool(toProve, jobs, async (c) => {
    const p = await prove(clang, c, yaml);
    if (p.status === "ok") proven++;
    if (p.status === "fail") {
      failed++;
      console.log(`FAIL ${c.file}\n  ${p.detail}`);
    }
  });
}

const secs = ((performance.now() - t0) / 1000).toFixed(1);
console.log(`${cmds.length} TUs (${astOnly.size} CIR could not fully compile, ${skipped.size} without an export), ${secs}s`);
console.log(`${proposals.size} parameters proposed, ${live.size} rewritten, ` +
            `${proposals.size - live.size} blocked`);
console.log(`${proven} TUs proven unchanged, ${failed} failed, ${toProve.length - proven - failed} skipped`);
if (blockedBy.size) {
  console.log("not rewritten:");
  for (const [why, n] of [...blockedBy].sort((x, y) => y[1] - x[1])) console.log(`  ${String(n).padStart(6)}  ${why}`);
}
if (verbose)
  for (const k of live) {
    const c = proposals.get(k)!;
    console.log(`  ${c.location}: ${c.kind} ${c.function}(${c.param})`);
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
