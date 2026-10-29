// check: prove -fcir-modernize rewrites change only spelling.
//
// For each translation unit: run clang with -fcir-modernize and collect its
// fix-its, apply them to copies of the touched files (mounted over the
// originals with -ivfsoverlay, so the source tree is never modified), then
// compile the original and the rewritten TU with classic codegen at -O0 and
// require the LLVM IR to be identical after two normalizations:
//
//  * pointer-to-reference changes the mangled name (P4Node -> R4Node); symbols
//    are paired by order of first appearance and must demangle to the same
//    name once '&' is read as '*';
//  * a reference parameter or argument gains nonnull/align/dereferenceable,
//    which are stripped from signatures and call lines only.
//
// Classic codegen is deliberately independent of CIR: the analysis that
// proposed the edit is not the one that checks it.
//
// usage: bun check.ts --clang <clang> -p <compile_commands.json> [-j N] [files or globs...]
//        bun check.ts --clang <clang> -- <file> <flags...>
import { spawn, spawnSync } from "child_process";
import { mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "fs";
import { tmpdir } from "os";
import { dirname, isAbsolute, join, resolve } from "path";

type Cmd = { file: string; dir: string; args: string[] };
type FixIt = { file: string; l1: number; c1: number; l2: number; c2: number; text: string };
type Result = {
  file: string;
  status: "ok" | "fail" | "none" | "skip";
  rewrites: number;
  detail?: string;
};

const argv = process.argv.slice(2);
let clang = "", compdb = "", jobs = navigator.hardwareConcurrency, verbose = false;
const patterns: string[] = [];
let direct: string[] | null = null;
for (let i = 0; i < argv.length; i++) {
  const a = argv[i];
  if (a === "--clang") clang = resolve(argv[++i]);
  else if (a === "-p") compdb = resolve(argv[++i]);
  else if (a === "-j") jobs = +argv[++i];
  else if (a === "-v") verbose = true;
  else if (a === "--") { direct = argv.slice(i + 1); break; }
  else patterns.push(a);
}
if (!clang || (!compdb && !direct)) {
  console.error("usage: bun check.ts --clang <clang> -p <compile_commands.json> [files...]\n" +
                "       bun check.ts --clang <clang> -- <file> <flags...>");
  process.exit(2);
}
const cxxfilt = join(dirname(clang), "llvm-cxxfilt");

// Flags that name outputs or dependency files, which both compiles replace.
const DROP_WITH_ARG = new Set(["-o", "-MF", "-MT", "-MQ"]);
const DROP = new Set(["-c", "-MD", "-MMD", "-S", "-E"]);

function loadCommands(): Cmd[] {
  if (direct) {
    const [file, ...args] = direct;
    return [{ file: resolve(file), dir: process.cwd(), args }];
  }
  const db = JSON.parse(readFileSync(compdb, "utf8")) as
    { file: string; directory: string; arguments?: string[]; command?: string }[];
  const globs = patterns.map((p) => new Bun.Glob(p));
  const out: Cmd[] = [];
  for (const e of db) {
    const file = isAbsolute(e.file) ? e.file : join(e.directory, e.file);
    if (globs.length && !globs.some((g) => g.match(file))) continue;
    const raw = e.arguments ?? splitCommand(e.command!);
    const args: string[] = [];
    for (let i = 1; i < raw.length; i++) {
      if (DROP_WITH_ARG.has(raw[i])) { i++; continue; }
      // Build-speed flags (semninja, codegen cache) would write next to the
      // project's own objects.
      if (raw[i] === "-Xclang" && /^-f(semantic-deps|codegen-cache-path)/.test(raw[i + 1] ?? "")) {
        i++;
        continue;
      }
      // CMake's precompiled header was built by the project's compiler, which
      // need not be this clang. Every TU includes its own headers anyway.
      if (raw[i] === "-Xclang" && /^-include(-pch)?$/.test(raw[i + 1] ?? "") &&
          raw[i + 2] === "-Xclang" && /cmake_pch\.hxx/.test(raw[i + 3] ?? "")) {
        i += 3;
        continue;
      }
      if (DROP.has(raw[i]) || raw[i] === e.file || raw[i] === file) continue;
      args.push(raw[i]);
    }
    out.push({ file, dir: e.directory, args });
  }
  return out;
}

function splitCommand(s: string): string[] {
  const out: string[] = [];
  let cur = "", q = "", any = false;
  for (let i = 0; i < s.length; i++) {
    const ch = s[i];
    if (q) {
      if (ch === q) q = "";
      else if (ch === "\\" && q === '"' && i + 1 < s.length) cur += s[++i];
      else cur += ch;
    } else if (ch === '"' || ch === "'") { q = ch; any = true; }
    else if (ch === "\\" && i + 1 < s.length) { cur += s[++i]; any = true; }
    else if (/\s/.test(ch)) { if (cur || any) out.push(cur); cur = ""; any = false; }
    else { cur += ch; any = true; }
  }
  if (cur || any) out.push(cur);
  return out;
}

function run(cmd: string, args: string[], cwd: string, input?: string):
    Promise<{ code: number; out: string; err: string }> {
  return new Promise((res) => {
    const p = spawn(cmd, args, { cwd });
    let out = "", err = "";
    p.stdout.on("data", (d) => (out += d));
    p.stderr.on("data", (d) => (err += d));
    p.on("close", (code) => res({ code: code ?? 1, out, err }));
    if (input !== undefined) p.stdin.end(input);
  });
}

// fix-it:"<file>":{l1:c1-l2:c2}:"<text>", text escaped as clang's
// TextDiagnostic::emitParseableFixits writes it.
const FIXIT = /^fix-it:"((?:[^"\\]|\\.)*)":\{(\d+):(\d+)-(\d+):(\d+)\}:"((?:[^"\\]|\\.)*)"$/;
function unescape(s: string): string {
  return s.replace(/\\(.)/g, (_, c) => (c === "n" ? "\n" : c === "t" ? "\t" : c));
}
function parseFixIts(stderr: string, cwd: string): FixIt[] {
  const out: FixIt[] = [];
  for (const line of stderr.split("\n")) {
    const m = FIXIT.exec(line);
    if (!m) continue;
    const f = unescape(m[1]);
    out.push({ file: isAbsolute(f) ? f : join(cwd, f), l1: +m[2], c1: +m[3], l2: +m[4],
               c2: +m[5], text: unescape(m[6]) });
  }
  return out;
}

// Columns are 1-based byte columns, so splice on a Buffer, not a string.
function applyFixIts(src: Buffer, fixes: FixIt[]): Buffer {
  const lineStart = [0];
  for (let i = 0; i < src.length; i++) if (src[i] === 10) lineStart.push(i + 1);
  const off = (l: number, c: number) => lineStart[l - 1] + c - 1;
  // Edits are spliced back to front. Two insertions at one offset must read in
  // their reported order, so the later-reported one is spliced first.
  const edits = fixes.map((f, i) => ({ b: off(f.l1, f.c1), e: off(f.l2, f.c2), t: f.text, i }))
    .sort((x, y) => y.b - x.b || y.e - x.e || y.i - x.i);
  let out = src, prevBegin = Infinity;
  for (const ed of edits) {
    if (ed.e > prevBegin) throw new Error(`overlapping fix-its at byte ${ed.b}`);
    out = Buffer.concat([out.subarray(0, ed.b), Buffer.from(ed.t), out.subarray(ed.e)]);
    prevBegin = ed.b;
  }
  return out;
}

function overlay(dir: string, fixes: FixIt[]): string {
  const byFile = new Map<string, FixIt[]>();
  for (const f of fixes) byFile.set(f.file, [...(byFile.get(f.file) ?? []), f]);
  const roots = [...byFile].map(([file, fs], i) => {
    const copy = join(dir, `${i}.src`);
    writeFileSync(copy, applyFixIts(readFileSync(file), dedupe(fs)));
    return { type: "file", name: file, "external-contents": copy };
  });
  const yaml = join(dir, "overlay.yaml");
  writeFileSync(yaml, JSON.stringify({ version: 0, "use-external-names": false, roots }));
  return yaml;
}

// A header edit is reported once per diagnostic that needs it; identical
// edits from two candidates are one edit.
function dedupe(fs: FixIt[]): FixIt[] {
  const seen = new Set<string>();
  return fs.filter((f) => {
    const k = `${f.l1}:${f.c1}-${f.l2}:${f.c2}:${f.text}`;
    return !seen.has(k) && (seen.add(k), true);
  });
}

const REF_ATTRS = / (nonnull|align \d+|dereferenceable\(\d+\)|dereferenceable_or_null\(\d+\))(?=[ ,)])/g;
// A load of a reference from its -O0 stack slot is tagged with the same facts
// as metadata. The numbered nodes those tags add are dropped on both sides;
// with -g0 the rest is module flags and ident, identical in both.
const REF_LOAD_MD = /, !(nonnull|align|noundef) !\d+/g;
function normalize(ir: string): string[] {
  const lines = ir.split("\n")
    .filter((l) => !l.startsWith("; ModuleID") && !l.startsWith("source_filename") && !/^!\d+ = /.test(l))
    .map((l) => /^(define|declare) |[ =]call |[ =]invoke /.test(l) ? l.replace(REF_ATTRS, "") : l)
    .map((l) => / = load ptr, /.test(l) ? l.replace(REF_LOAD_MD, "") : l);
  return renumberTypes(lines);
}

// Named struct types get a numeric suffix (ArrayRef.1198) in creation order,
// which shifts when an unrelated type is created earlier. Each base name is
// renumbered by order of first appearance, the same way in both modules.
const NUMBERED_TYPE = /%("?)((?:struct|class|union)\.[^"\s,)]*?)\.(\d+)\1(?=[\s,)*\]}]|$)/g;
function renumberTypes(lines: string[]): string[] {
  const ids = new Map<string, string>(), count = new Map<string, number>();
  return lines.map((l) => l.replace(NUMBERED_TYPE, (_, q, base, n) => {
    const key = `${base}.${n}`;
    if (!ids.has(key)) {
      const k = (count.get(base) ?? 0) + 1;
      count.set(base, k);
      ids.set(key, `%${q}${base}.#${k}${q}`);
    }
    return ids.get(key)!;
  }));
}

// Quoted symbols (lambdas, `$` in the name) count too: a lambda's mangled
// name embeds its enclosing function's parameter types.
function symbols(lines: string[]): string[] {
  const seen = new Set<string>();
  for (const l of lines) for (const m of l.matchAll(/@"?(_Z[\w.$]+)/g)) seen.add(m[1]);
  return [...seen];
}

function demangle(names: string[]): string[] {
  if (!names.length) return [];
  const r = spawnSync(cxxfilt, [], { input: names.join("\n") + "\n" });
  return r.stdout.toString().trimEnd().split("\n");
}

// Rename the rewritten module's symbols to the original's, or say why the
// symbol lists do not pair up. A mangled name also appears inside other
// symbols (`@__func__._ZL5firstP4Node` from assert), so it is renamed wherever
// it follows '@' or '.'.
function remap(orig: string[], next: string[]): string[] | string {
  const a = symbols(orig), b = symbols(next);
  if (a.length !== b.length) return `symbol count ${a.length} vs ${b.length}`;
  const da = demangle(a), db = demangle(b);
  const rename = new Map<string, string>();
  for (let i = 0; i < a.length; i++) {
    if (a[i] === b[i]) continue;
    if (da[i].replace(/&/g, "*") !== db[i].replace(/&/g, "*")) return `symbol ${i}: ${da[i]} vs ${db[i]}`;
    rename.set(b[i], a[i]);
  }
  if (!rename.size) return next;
  const re = new RegExp(`(?<=[@."])(${[...rename.keys()].map((k) => k.replace(/[$.]/g, "\\$&")).join("|")})(?![\\w$])`, "g");
  return next.map((l) => l.replace(re, (n) => rename.get(n)!));
}

function firstDiff(a: string[], b: string[]): string {
  let fn = "";
  for (let i = 0; i < Math.max(a.length, b.length); i++) {
    if (a[i]?.startsWith("define ")) fn = a[i];
    if (a[i] !== b[i]) return `in ${fn || "module"}\n  - ${a[i]}\n  + ${b[i]}`;
  }
  return "";
}

const IR_FLAGS = ["-S", "-emit-llvm", "-O0", "-g0", "-o", "-"];

async function check(c: Cmd, scratch: string): Promise<Result> {
  const m = await run(clang, [...c.args, "-fclangir", "-S", "-emit-cir", "-fcir-modernize",
                               "-fdiagnostics-parseable-fixits", "-fno-color-diagnostics", "-o", "/dev/null", c.file], c.dir);
  if (m.code !== 0) {
    const why = m.err.split("\n").find((l) => /error|NYI|Assertion|LLVM ERROR/.test(l)) ?? `exit ${m.code}`;
    return { file: c.file, status: "skip", rewrites: 0, detail: why.trim() };
  }
  const rewrites = (m.err.match(/warning: pointer parameter /g) ?? []).length;
  const fixes = parseFixIts(m.err, c.dir);
  if (!fixes.length) return { file: c.file, status: "none", rewrites };
  const dir = mkdtempSync(join(scratch, "tu-"));
  try {
    const yaml = overlay(dir, fixes);
    const [a, b] = await Promise.all([
      run(clang, [...c.args, ...IR_FLAGS, c.file], c.dir),
      run(clang, [...c.args, "-ivfsoverlay", yaml, ...IR_FLAGS, c.file], c.dir),
    ]);
    if (a.code !== 0) return { file: c.file, status: "skip", rewrites, detail: "original does not compile" };
    if (b.code !== 0) {
      const why = b.err.split("\n").filter((l) => l.includes("error:")).slice(0, 3).join("\n  ");
      return { file: c.file, status: "fail", rewrites, detail: `rewritten TU does not compile:\n  ${why}` };
    }
    const orig = normalize(a.out), next = remap(orig, normalize(b.out));
    if (typeof next === "string") return { file: c.file, status: "fail", rewrites, detail: next };
    const d = firstDiff(orig, next);
    return d ? { file: c.file, status: "fail", rewrites, detail: d } : { file: c.file, status: "ok", rewrites };
  } finally {
    if (!verbose) rmSync(dir, { recursive: true, force: true });
  }
}

const cmds = loadCommands();
const scratch = mkdtempSync(join(tmpdir(), "cirmodernize-"));
mkdirSync(scratch, { recursive: true });
const results: Result[] = [];
let next = 0;
await Promise.all(Array.from({ length: Math.min(jobs, cmds.length) }, async () => {
  while (next < cmds.length) {
    const r = await check(cmds[next++], scratch);
    results.push(r);
    if (r.status === "fail" || r.status === "ok" || verbose)
      console.log(`${r.status.toUpperCase().padEnd(4)} ${r.file} (${r.rewrites} rewrites)` +
                  (r.detail ? `\n  ${r.detail}` : ""));
  }
}));
if (!verbose) rmSync(scratch, { recursive: true, force: true });

const count = (s: Result["status"]) => results.filter((r) => r.status === s).length;
const proven = results.filter((r) => r.status === "ok").reduce((n, r) => n + r.rewrites, 0);
console.log(`\n${results.length} TUs: ${count("ok")} proven, ${count("fail")} failed, ` +
            `${count("none")} no rewrites, ${count("skip")} skipped (CIR could not compile)`);
console.log(`${proven} rewrites proven identical`);
const skips = new Map<string, number>();
for (const r of results) if (r.status === "skip") skips.set(r.detail!, (skips.get(r.detail!) ?? 0) + 1);
for (const [why, n] of [...skips].sort((x, y) => y[1] - x[1]).slice(0, 8)) console.log(`  skip x${n}: ${why}`);
process.exit(count("fail") ? 1 : 0);
