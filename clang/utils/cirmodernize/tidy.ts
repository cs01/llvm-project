// tidy: prove clang-tidy's fix-its change no compiled code.
//
// clang-tidy already rewrites C-style casts, NULL, missing override and
// typedefs (modernize-use-nullptr, modernize-use-override,
// modernize-use-using, google-readability-casting). Its fixes come from the
// AST and are usually right; this keeps only the ones every translation unit
// that sees them compiles to the same IR with (lib.ts prove), and lists the
// rest.
//
// A fix is one diagnostic's replacements. Fixes inside an #if region (other
// than an include guard) are refused: the build compiled one configuration,
// and the fix may mean something else in another. A TU that fails drops every
// fix in the files it includes, and the rest are proven again.
//
// usage: bun tidy.ts --clang <clang> --clang-tidy <clang-tidy>
//                    -p <compile_commands.json> --root <dir>
//                    [--checks <checks>] [--apply] [-j N] [-v] [files or globs...]
import { mkdtempSync, readFileSync, writeFileSync, existsSync } from "fs";
import { tmpdir } from "os";
import { join, resolve, sep } from "path";
import {
  type Cmd, type Edit, applyEdits, byFile, defaultJobs, firstError, loadCommands, overlay, pool, prove, run,
} from "./lib.ts";

const argv = process.argv.slice(2);
let clang = "", tidy = "", compdb = "", root = process.cwd(), jobs = defaultJobs, apply = false, verbose = false;
let checks = "-*,modernize-use-nullptr,modernize-use-override,modernize-use-using,google-readability-casting";
const patterns: string[] = [];
for (let i = 0; i < argv.length; i++) {
  const a = argv[i];
  if (a === "--clang") clang = resolve(argv[++i]);
  else if (a === "--clang-tidy") tidy = resolve(argv[++i]);
  else if (a === "-p") compdb = resolve(argv[++i]);
  else if (a === "--root") root = resolve(argv[++i]);
  else if (a === "--checks") checks = argv[++i];
  else if (a === "--apply") apply = true;
  else if (a === "-j") jobs = +argv[++i];
  else if (a === "-v") verbose = true;
  else patterns.push(a);
}
if (!clang || !tidy || !compdb) {
  console.error("usage: bun tidy.ts --clang <clang> --clang-tidy <clang-tidy> -p <compile_commands.json> [options] [files...]");
  process.exit(2);
}

type Fix = { check: string; message: string; edits: Edit[] };
const under = (file: string, dir: string) => file === dir || file.startsWith(dir + sep);
const scratch = mkdtempSync(join(tmpdir(), "cirmodernize-tidy-"));
const t0 = performance.now();

// 1. collect fixes
const cmds = loadCommands(compdb, patterns);
const fixes = new Map<string, Fix>();
const tidyFailed: string[] = [];
await pool(cmds.map((c, i) => [c, i] as const), jobs, async ([c, i]) => {
  const out = join(scratch, `fixes-${i}.yaml`);
  // Everything after -- is the compile command; the header filter keeps
  // fixes in headers under --root.
  const r = await run(tidy, [`-checks=${checks}`, `-header-filter=^${root}/`, `-export-fixes=${out}`,
    "-quiet", c.file, "--", ...c.args], c.dir);
  if (!existsSync(out)) {
    if (r.code !== 0) tidyFailed.push(`${c.file}: ${firstError(r.err, r.code)}`);
    return;
  }
  const y = Bun.YAML.parse(readFileSync(out, "utf8")) as any;
  for (const d of y?.Diagnostics ?? []) {
    const m = d.DiagnosticMessage ?? {};
    const edits: Edit[] = (m.Replacements ?? []).map((x: any) => ({
      file: resolve(c.dir, x.FilePath), offset: x.Offset, length: x.Length, text: x.ReplacementText ?? "",
    }));
    if (!edits.length || !edits.every((e) => under(e.file, root))) continue;
    const key = edits.map((e) => `${e.file}:${e.offset}:${e.length}:${e.text}`).join("|");
    fixes.set(key, { check: d.DiagnosticName, message: m.Message, edits });
  }
});

const found = fixes.size;

// 2. refuse fixes inside #if regions, found from the directive lines
const regions = new Map<string, [number, number][]>();
function conditionalRegions(file: string): [number, number][] {
  if (regions.has(file)) return regions.get(file)!;
  const src = readFileSync(file, "utf8");
  const out: [number, number][] = [], open: { start: number; macro?: string }[] = [];
  const directive = /^[ \t]*#[ \t]*(if|ifdef|ifndef|elif|else|endif)\b[ \t]*(\w*)/gm;
  let first = true, guard: [number, number] | undefined;
  for (const m of src.matchAll(directive)) {
    const [, kind, word] = m;
    if (kind === "if" || kind === "ifdef" || kind === "ifndef") {
      open.push({ start: m.index!, macro: first && kind === "ifndef" ? word : undefined });
      first = false;
    } else if (kind === "endif" && open.length) {
      const o = open.pop()!;
      const region: [number, number] = [o.start, m.index! + m[0].length];
      // An #ifndef X first in the file, closed last, with #define X next is
      // an include guard.
      if (o.macro && !open.length && new RegExp(`^[ \\t]*#[ \\t]*define[ \\t]+${o.macro}\\b`, "m").test(src))
        guard = region;
      out.push(region);
    }
  }
  const last = out.reduce((a, r) => Math.max(a, r[1]), 0);
  const kept = out.filter((r) => !(guard && r === guard && src.slice(last).trim() === ""));
  regions.set(file, kept);
  return kept;
}
const refused = new Map<string, number>();
const refuse = (why: string) => refused.set(why, (refused.get(why) ?? 0) + 1);
for (const [k, f] of fixes)
  if (f.edits.some((e) => conditionalRegions(e.file).some(([a, b]) => e.offset < b && e.offset + e.length >= a))) {
    fixes.delete(k);
    refuse("inside an #if region");
  }
// Overlapping fixes cannot both apply; keep the first.
{
  const taken = new Map<string, [number, number][]>();
  for (const [k, f] of fixes) {
    const clash = f.edits.some((e) => (taken.get(e.file) ?? []).some(([a, b]) => e.offset < b && a < e.offset + Math.max(e.length, 1)));
    if (clash) {
      fixes.delete(k);
      refuse("overlaps another fix");
      continue;
    }
    for (const e of f.edits) {
      if (!taken.has(e.file)) taken.set(e.file, []);
      taken.get(e.file)!.push([e.offset, e.offset + Math.max(e.length, 1)]);
    }
  }
}

// 3. which TUs see which files
const all = loadCommands(compdb, []);
const deps = await pool(all, jobs, async (c) => {
  const r = await run(clang, [...c.args, "-M", "-o", "-", c.file], c.dir);
  if (r.code !== 0) return null; // might include anything
  return new Set(r.out.split(/\s+/).filter((d) => d && d !== "\\" && !d.endsWith(":")).map((d) => resolve(c.dir, d)));
});

// 4. prove, dropping fixes in what a failing TU sees
let failed = 0, proven = 0, live = new Map(fixes);
for (let round = 1; ; round++) {
  const edits = [...live.values()].flatMap((f) => f.edits);
  const files = new Set(edits.map((e) => e.file));
  const picked = all.map((c, i) => [c, i] as const).filter(([, i]) => !deps[i] || [...files].some((f) => deps[i]!.has(f)));
  failed = proven = 0;
  if (!edits.length) break;
  const yaml = overlay(mkdtempSync(join(scratch, "overlay-")), edits);
  const bad: number[] = [];
  await pool(picked, jobs, async ([c, i]) => {
    const p = await prove(clang, c, yaml);
    if (p.status === "ok") return void proven++;
    failed++;
    bad.push(i);
    console.log(`FAIL ${c.file}\n  ${p.detail}`);
  });
  if (!failed || round === 5) break;
  const seen = new Set(bad.flatMap((i) => deps[i] ? [...deps[i]!] : [...files]));
  let dropped = 0;
  for (const [k, f] of live)
    if (f.edits.some((e) => seen.has(e.file))) {
      live.delete(k);
      dropped++;
      refuse("a TU that sees it did not prove unchanged");
    }
  console.log(`round ${round}: ${failed} TUs failed, ${dropped} fixes dropped, proving the rest again`);
  if (!dropped) break;
}

const secs = ((performance.now() - t0) / 1000).toFixed(1);
const byCheck = new Map<string, number>();
for (const f of live.values()) byCheck.set(f.check, (byCheck.get(f.check) ?? 0) + 1);
console.log(`${cmds.length} TUs run through clang-tidy (${tidyFailed.length} failed), ${secs}s`);
console.log(`${found} fixes found, ${live.size} proven, ${proven} TUs proven unchanged, ${failed} failed`);
for (const [c, n] of [...byCheck].sort((a, b) => b[1] - a[1])) console.log(`  ${n} ${c}`);
if (refused.size) {
  console.log("not applied:");
  for (const [w, n] of [...refused].sort((a, b) => b[1] - a[1])) console.log(`  ${n} ${w}`);
}
if (verbose) for (const f of live.values()) console.log(`  ${f.edits[0].file}:${f.edits[0].offset} ${f.check}: ${f.message}`);
if (apply && !failed && live.size) {
  for (const [file, es] of byFile([...live.values()].flatMap((f) => f.edits)))
    writeFileSync(file, applyEdits(readFileSync(file), es));
  console.log(`applied ${live.size} fixes`);
}
