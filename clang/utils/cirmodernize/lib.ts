// Shared by check.ts and project.ts: compile commands, edits, and the proof
// that a rewritten translation unit compiles to the same LLVM IR.
//
// The proof compiles the original and the rewritten TU with classic codegen at
// -O0 and requires identical IR after two normalizations:
//
//  * pointer-to-reference changes the mangled name (P4Node -> R4Node); symbols
//    are paired by order of first appearance and must demangle to the same
//    name once '&' is read as '*';
//  * a reference parameter or argument gains nonnull/align/dereferenceable,
//    which are stripped from signatures and call lines only.
//
// Classic codegen is deliberately independent of CIR: the analysis that
// proposed the edit is not the one that checks it.
import { spawn, spawnSync } from "child_process";
import { readFileSync, writeFileSync } from "fs";
import { dirname, isAbsolute, join } from "path";

export type Cmd = { file: string; dir: string; args: string[] };
// A byte range of a file replaced by text; length 0 is an insertion.
export type Edit = { file: string; offset: number; length: number; text: string };

// Flags that name outputs or dependency files, which every compile here
// replaces.
const DROP_WITH_ARG = new Set(["-o", "-MF", "-MT", "-MQ"]);
const DROP = new Set(["-c", "-MD", "-MMD", "-S", "-E"]);

export function loadCommands(compdb: string, patterns: string[]): Cmd[] {
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

export function run(cmd: string, args: string[], cwd: string):
    Promise<{ code: number; out: string; err: string }> {
  return new Promise((res) => {
    const p = spawn(cmd, args, { cwd });
    let out = "", err = "";
    p.stdout.on("data", (d) => (out += d));
    p.stderr.on("data", (d) => (err += d));
    p.on("close", (code) => res({ code: code ?? 1, out, err }));
  });
}

// Runs fn over items with at most `jobs` in flight.
export async function pool<T, R>(items: T[], jobs: number, fn: (t: T) => Promise<R>): Promise<R[]> {
  const out: R[] = new Array(items.length);
  let next = 0;
  await Promise.all(Array.from({ length: Math.min(jobs, items.length) }, async () => {
    while (next < items.length) {
      const i = next++;
      out[i] = await fn(items[i]);
    }
  }));
  return out;
}

// Identical edits reported by two candidates, or two translation units that
// both see a header, are one edit.
export function dedupe(edits: Edit[]): Edit[] {
  const seen = new Set<string>();
  return edits.filter((e) => {
    const k = `${e.file}:${e.offset}:${e.length}:${e.text}`;
    return !seen.has(k) && (seen.add(k), true);
  });
}

// Edits are spliced back to front. Two insertions at one offset must read in
// their given order, so the later one is spliced first.
export function applyEdits(src: Buffer, edits: Edit[]): Buffer {
  const sorted = edits.map((e, i) => ({ b: e.offset, e: e.offset + e.length, t: e.text, i }))
    .sort((x, y) => y.b - x.b || y.e - x.e || y.i - x.i);
  let out = src, prevBegin = Infinity;
  for (const ed of sorted) {
    if (ed.e > prevBegin) throw new Error(`overlapping edits at byte ${ed.b}`);
    out = Buffer.concat([out.subarray(0, ed.b), Buffer.from(ed.t), out.subarray(ed.e)]);
    prevBegin = ed.b;
  }
  return out;
}

export function byFile(edits: Edit[]): Map<string, Edit[]> {
  const m = new Map<string, Edit[]>();
  for (const e of dedupe(edits)) m.set(e.file, [...(m.get(e.file) ?? []), e]);
  return m;
}

// Writes rewritten copies into dir and a VFS overlay that mounts them over
// the originals, so the source tree is never modified.
export function overlay(dir: string, edits: Edit[]): string {
  const roots = [...byFile(edits)].map(([file, es], i) => {
    const copy = join(dir, `${i}.src`);
    writeFileSync(copy, applyEdits(readFileSync(file), es));
    return { type: "file", name: file, "external-contents": copy };
  });
  const yaml = join(dir, "overlay.yaml");
  writeFileSync(yaml, JSON.stringify({ version: 0, "use-external-names": false, roots }));
  return yaml;
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
// name embeds its enclosing function's parameter types. So do COMDAT names
// (`$_Z4peekP4Node`), which inline functions get.
function symbols(lines: string[]): string[] {
  const seen = new Set<string>();
  for (const l of lines) for (const m of l.matchAll(/[@$]"?(_Z[\w.$]+)/g)) seen.add(m[1]);
  return [...seen];
}

function demangle(cxxfilt: string, names: string[]): string[] {
  if (!names.length) return [];
  const r = spawnSync(cxxfilt, [], { input: names.join("\n") + "\n" });
  return r.stdout.toString().trimEnd().split("\n");
}

// Rename the rewritten module's symbols to the original's, or say why the
// symbol lists do not pair up. A mangled name also appears inside other
// symbols (`@__func__._ZL5firstP4Node` from assert), so it is renamed wherever
// it follows '@', '$', '.' or a quote.
function remap(cxxfilt: string, orig: string[], next: string[]): string[] | string {
  const a = symbols(orig), b = symbols(next);
  if (a.length !== b.length) return `symbol count ${a.length} vs ${b.length}`;
  const da = demangle(cxxfilt, a), db = demangle(cxxfilt, b);
  const rename = new Map<string, string>();
  for (let i = 0; i < a.length; i++) {
    if (a[i] === b[i]) continue;
    if (da[i].replace(/&/g, "*") !== db[i].replace(/&/g, "*")) return `symbol ${i}: ${da[i]} vs ${db[i]}`;
    rename.set(b[i], a[i]);
  }
  if (!rename.size) return next;
  const re = new RegExp(`(?<=[@."$])(${[...rename.keys()].map((k) => k.replace(/[$.]/g, "\\$&")).join("|")})(?![\\w$])`, "g");
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

export type Proof = { status: "ok" | "fail" | "skip"; detail?: string };

// Compiles c as is and with the overlay mounted, and compares the IR.
export async function prove(clang: string, c: Cmd, yaml: string): Promise<Proof> {
  const [a, b] = await Promise.all([
    run(clang, [...c.args, ...IR_FLAGS, c.file], c.dir),
    run(clang, [...c.args, "-ivfsoverlay", yaml, ...IR_FLAGS, c.file], c.dir),
  ]);
  if (a.code !== 0) return { status: "skip", detail: "original does not compile" };
  if (b.code !== 0) {
    const why = b.err.split("\n").filter((l) => l.includes("error:")).slice(0, 3).join("\n  ");
    return { status: "fail", detail: `rewritten TU does not compile:\n  ${why}` };
  }
  const cxxfilt = join(dirname(clang), "llvm-cxxfilt");
  const orig = normalize(a.out), next = remap(cxxfilt, orig, normalize(b.out));
  if (typeof next === "string") return { status: "fail", detail: next };
  const d = firstDiff(orig, next);
  return d ? { status: "fail", detail: d } : { status: "ok" };
}

// The first line of a failed compile that says why.
export function firstError(err: string, code: number): string {
  return (err.split("\n").find((l) => /error|NYI|Assertion|LLVM ERROR/.test(l)) ?? `exit ${code}`).trim();
}
