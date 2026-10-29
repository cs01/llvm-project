// Shared by check.ts and project.ts: compile commands, edits, and the proof
// that a rewritten translation unit compiles to the same LLVM IR.
//
// The proof compiles the original and the rewritten TU with classic codegen at
// -O0, and clang/tools/cir-modernize-ircmp requires the modules to print
// identically once the few differences a spelling change causes (mangled
// names, reference attributes and load metadata, struct type suffixes) are
// undone; that tool lists them.
//
// Classic codegen is deliberately independent of CIR: the analysis that
// proposed the edit is not the one that checks it.
import { spawn } from "child_process";
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "fs";
import { tmpdir, totalmem } from "os";
import { dirname, isAbsolute, join, resolve } from "path";

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
    // CMake's precompiled-header jobs build a .pch, not code: nothing to
    // prove.
    if (/cmake_pch\.(hxx|h)\.(cxx|c)$/.test(file) || raw.some((a) => a === "-emit-pch")) continue;
    const args: string[] = [];
    for (let i = 1; i < raw.length; i++) {
      // C++20 module scanning (CMake 3.28) passes @<obj>.modmap, which only a
      // build writes; a TU that imports no module does not need it.
      if (/^@.*\.modmap$/.test(raw[i]) && !existsSync(join(e.directory, raw[i].slice(1)))) continue;
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
      if (DROP.has(raw[i]) || raw[i] === e.file || resolve(e.directory, raw[i]) === file) continue;
      args.push(raw[i]);
    }
    // A project's -Werror would turn this clang's own warnings, or the
    // unused -S next to -emit-cir, into failures. Warnings never change IR.
    args.push("-Wno-error", "-Wno-unused-command-line-argument");
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

// A CIR compile of a big TU can take 4-10 GB, and ten at once took a 16 GB
// machine into swap until the kernel watchdog panicked. So the default job
// count fits RAM, new compiles wait while running ones already hold most of
// it, and a compile that grows past PER_JOB_CAP is killed and reported.
const GB = 1024 ** 3;
const RAM = totalmem();
export const defaultJobs = Math.max(1, Math.min(navigator.hardwareConcurrency, Math.floor(RAM / (4 * GB))));
const BUDGET = RAM * 0.6;
const PER_JOB_CAP = Math.min(8 * GB, RAM * 0.45);
const live = new Map<number, number>(); // process group -> resident bytes
let poller: ReturnType<typeof setInterval> | undefined;

function sample() {
  // Sum by process group so a driver that spawns -cc1 is counted whole.
  const ps = Bun.spawnSync(["ps", "-A", "-o", "pgid=,rss="]).stdout.toString();
  const rss = new Map<number, number>();
  for (const line of ps.split("\n")) {
    const [g, k] = line.trim().split(/\s+/).map(Number);
    if (live.has(g)) rss.set(g, (rss.get(g) ?? 0) + k * 1024);
  }
  for (const g of live.keys()) {
    const b = rss.get(g) ?? 0;
    live.set(g, b);
    if (b > PER_JOB_CAP) {
      console.error(`cir-modernize: killing pid ${g}, ${(b / GB).toFixed(1)} GB resident`);
      try { process.kill(-g, "SIGKILL"); } catch {}
    }
  }
}

const used = () => [...live.values()].reduce((a, b) => a + b, 0);

export async function run(cmd: string, args: string[], cwd: string):
    Promise<{ code: number; out: string; err: string }> {
  while (live.size > 0 && used() > BUDGET) await Bun.sleep(500);
  poller ??= setInterval(sample, 1000);
  return new Promise((res) => {
    const p = spawn(cmd, args, { cwd, detached: true });
    const g = p.pid!;
    live.set(g, 0);
    let out = "", err = "";
    p.stdout.on("data", (d) => (out += d));
    p.stderr.on("data", (d) => (err += d));
    p.on("close", (code, signal) => {
      live.delete(g);
      if (live.size === 0 && poller) { clearInterval(poller); poller = undefined; }
      if (signal === "SIGKILL") err = `killed: resident memory over ${(PER_JOB_CAP / GB).toFixed(1)} GB\n` + err;
      res({ code: code ?? 1, out, err });
    });
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

const IR_FLAGS = ["-emit-llvm", "-c", "-O0", "-g0", "-fno-color-diagnostics"];
const REFINE_FLAGS = ["-emit-llvm", "-c", "-O1", "-Xclang", "-disable-llvm-passes", "-g0", "-fno-color-diagnostics"];

export type Proof = { status: "ok" | "fail" | "skip"; detail?: string };

// Compiles c as is and with the overlay mounted, and has cir-modernize-ircmp
// compare the two modules.
// extra goes to both compiles, for warnings the gate should compare. refine
// is for rewrites that change the code: both modules are compiled without
// optnone and compared after cir-modernize-ircmp --refine's normalization.
export async function prove(clang: string, c: Cmd, yaml: string, extra: string[] = [],
                            refine = false): Promise<Proof> {
  const flags = refine ? REFINE_FLAGS : IR_FLAGS;
  const dir = mkdtempSync(join(tmpdir(), "cirmodernize-prove-"));
  try {
    const orig = join(dir, "orig.bc"), next = join(dir, "next.bc");
    const [a, b] = await Promise.all([
      run(clang, [...c.args, ...extra, ...flags, c.file, "-o", orig], c.dir),
      run(clang, [...c.args, ...extra, "-ivfsoverlay", yaml, ...flags, c.file, "-o", next], c.dir),
    ]);
    if (a.code !== 0) return { status: "skip", detail: `original does not compile: ${firstError(a.err, a.code)}` };
    if (b.code !== 0) {
      const why = b.err.split("\n").filter((l) => l.includes("error:")).slice(0, 3).join("\n  ");
      return { status: "fail", detail: `rewritten TU does not compile:\n  ${why}` };
    }
    // An edit can make the compiler warn where it did not, which breaks a
    // -Werror build even though the IR is the same: a qualifier that makes
    // -Wnullability-completeness ask for every other pointer in a header.
    const added = newWarnings(a.err, b.err);
    if (added) return { status: "fail", detail: `new warning: ${added}` };
    const r = await run(join(dirname(clang), "cir-modernize-ircmp"), [...(refine ? ["--refine"] : []), orig, next], dir);
    if (r.code === 0) return { status: "ok" };
    return { status: "fail", detail: (r.out || r.err).trimEnd() };
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

// The first warning in next that orig does not have as often, by message; the
// locations move with the edits.
// The first warning the rewritten TU gives that the original does not, with
// its location. Warnings are matched by message alone, since an edit moves
// the columns after it.
function newWarnings(orig: string, next: string): string | undefined {
  const lines = (err: string) => err.split("\n").filter((l) => l.includes(" warning: "));
  const message = (l: string) => l.slice(l.indexOf(" warning: ") + 10);
  const count = new Map<string, number>();
  for (const l of lines(orig)) count.set(message(l), (count.get(message(l)) ?? 0) + 1);
  for (const l of lines(next)) {
    const n = count.get(message(l)) ?? 0;
    if (!n) return l.trim();
    count.set(message(l), n - 1);
  }
  return undefined;
}

// The first line of a failed compile that says why.
export function firstError(err: string, code: number): string {
  return (err.split("\n").find((l) => /error|NYI|Assertion|LLVM ERROR/.test(l)) ?? `exit ${code}`).trim();
}
