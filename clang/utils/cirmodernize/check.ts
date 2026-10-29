// check: prove each translation unit's -fcir-modernize rewrites change only
// spelling.
//
// For each TU: run clang with -fcir-modernize and collect its fix-its, apply
// them over the originals with -ivfsoverlay, and prove the IR unchanged (see
// lib.ts). Only the rewrites one TU can make alone are checked here; project.ts
// does the ones that span translation units.
//
// --why (assertions builds only) also totals, over the whole run, why the
// parameters CIR proved must-dereference were not rewritten.
//
// usage: bun check.ts --clang <clang> -p <compile_commands.json> [-j N] [--why]
//                     [files or globs...]
//        bun check.ts --clang <clang> -- <file> <flags...>
import { mkdtempSync, readFileSync, rmSync } from "fs";
import { tmpdir } from "os";
import { isAbsolute, join, resolve } from "path";
import { type Cmd, type Edit, firstError, loadCommands, overlay, pool, prove, run } from "./lib.ts";

type Result = {
  file: string;
  status: "ok" | "fail" | "none" | "skip";
  rewrites: number;
  detail?: string;
};

const argv = process.argv.slice(2);
let clang = "", compdb = "", jobs = navigator.hardwareConcurrency, verbose = false, why = false;
const patterns: string[] = [];
let direct: string[] | null = null;
for (let i = 0; i < argv.length; i++) {
  const a = argv[i];
  if (a === "--clang") clang = resolve(argv[++i]);
  else if (a === "-p") compdb = resolve(argv[++i]);
  else if (a === "-j") jobs = +argv[++i];
  else if (a === "-v") verbose = true;
  else if (a === "--why") why = true;
  else if (a === "--") { direct = argv.slice(i + 1); break; }
  else patterns.push(a);
}
if (!clang || (!compdb && !direct)) {
  console.error("usage: bun check.ts --clang <clang> -p <compile_commands.json> [files...]\n" +
                "       bun check.ts --clang <clang> -- <file> <flags...>");
  process.exit(2);
}

// fix-it:"<file>":{l1:c1-l2:c2}:"<text>", text escaped as clang's
// TextDiagnostic::emitParseableFixits writes it.
const FIXIT = /^fix-it:"((?:[^"\\]|\\.)*)":\{(\d+):(\d+)-(\d+):(\d+)\}:"((?:[^"\\]|\\.)*)"$/;
function unescape(s: string): string {
  return s.replace(/\\(.)/g, (_, c) => (c === "n" ? "\n" : c === "t" ? "\t" : c));
}

// Columns are 1-based byte columns, so offsets come from the file's bytes.
function parseFixIts(stderr: string, cwd: string): Edit[] {
  const lineStarts = new Map<string, number[]>();
  const starts = (file: string) => {
    let s = lineStarts.get(file);
    if (!s) {
      const src = readFileSync(file);
      s = [0];
      for (let i = 0; i < src.length; i++) if (src[i] === 10) s.push(i + 1);
      lineStarts.set(file, s);
    }
    return s;
  };
  const out: Edit[] = [];
  for (const line of stderr.split("\n")) {
    const m = FIXIT.exec(line);
    if (!m) continue;
    const f = unescape(m[1]), file = isAbsolute(f) ? f : join(cwd, f);
    const s = starts(file), b = s[+m[2] - 1] + +m[3] - 1, e = s[+m[4] - 1] + +m[5] - 1;
    out.push({ file, offset: b, length: e - b, text: unescape(m[6]) });
  }
  return out;
}

const drops = new Map<string, number>();

async function check(c: Cmd, scratch: string): Promise<Result> {
  const m = await run(clang, [
    ...c.args, "-fclangir", "-S", "-emit-cir", "-fcir-modernize",
    "-fdiagnostics-parseable-fixits", "-fno-color-diagnostics",
    ...(why ? ["-mllvm", "-debug-only=cir-modernize"] : []),
    "-o", "/dev/null", c.file,
  ], c.dir);
  for (const [, n, reason] of m.err.matchAll(/^cir-modernize: (\d+) (.*)$/gm))
    drops.set(reason, (drops.get(reason) ?? 0) + +n);
  if (m.code !== 0) return { file: c.file, status: "skip", rewrites: 0, detail: firstError(m.err, m.code) };
  const rewrites = (m.err.match(/warning: pointer parameter /g) ?? []).length;
  const edits = parseFixIts(m.err, c.dir);
  if (!edits.length) return { file: c.file, status: "none", rewrites };
  const dir = mkdtempSync(join(scratch, "tu-"));
  try {
    const p = await prove(clang, c, overlay(dir, edits));
    return { file: c.file, status: p.status, rewrites, detail: p.detail };
  } finally {
    if (!verbose) rmSync(dir, { recursive: true, force: true });
  }
}

const cmds: Cmd[] = direct
  ? [{ file: resolve(direct[0]), dir: process.cwd(), args: direct.slice(1) }]
  : loadCommands(compdb, patterns);
const scratch = mkdtempSync(join(tmpdir(), "cirmodernize-"));
const results = await pool(cmds, jobs, async (c) => {
  const r = await check(c, scratch);
  if (r.status === "fail" || r.status === "ok" || verbose)
    console.log(`${r.status.toUpperCase().padEnd(4)} ${r.file} (${r.rewrites} rewrites)` +
                (r.detail ? `\n  ${r.detail}` : ""));
  return r;
});
if (!verbose) rmSync(scratch, { recursive: true, force: true });

const count = (s: Result["status"]) => results.filter((r) => r.status === s).length;
const proven = results.filter((r) => r.status === "ok").reduce((n, r) => n + r.rewrites, 0);
console.log(`\n${results.length} TUs: ${count("ok")} proven, ${count("fail")} failed, ` +
            `${count("none")} no rewrites, ${count("skip")} skipped (CIR could not compile)`);
console.log(`${proven} rewrites proven identical`);
const skips = new Map<string, number>();
for (const r of results) if (r.status === "skip") skips.set(r.detail!, (skips.get(r.detail!) ?? 0) + 1);
for (const [w, n] of [...skips].sort((x, y) => y[1] - x[1]).slice(0, 8)) console.log(`  skip x${n}: ${w}`);
if (why) {
  console.log("\nmust-dereference parameters, by why they were not rewritten:");
  for (const [reason, n] of [...drops].sort((x, y) => y[1] - x[1]))
    console.log(`  ${String(n).padStart(6)}  ${reason}`);
}
process.exit(count("fail") ? 1 : 0);
