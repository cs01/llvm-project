// The acceptance gate: compiles every program in the corpus (about 10,000, written to break Safe C in three red-team runs)
// and compares what the compiler accepts with verdicts.txt. A change that makes the compiler accept a program it used to
// reject has loosened a rule; that is where holes come from, so each one must be listed, with a reason, in accepted.txt,
// which only a maintainer edits. A newly accepted program that also has a memory error under ASan is a hole and fails
// even if listed. New compiler crashes fail. Newly rejected programs are reported (tightening needs no sign-off).
//
//   bun clang/utils/safe-c/corpus/check.ts            check (SAFEC= another compiler)
//   bun clang/utils/safe-c/corpus/check.ts --update   rewrite verdicts.txt after the check passes
//   bun clang/utils/safe-c/corpus/check.ts --only=core/holes/x.c   one program
import { readFileSync, writeFileSync, mkdtempSync, mkdirSync, readdirSync } from "fs";
import { join, dirname } from "path";
import { tmpdir, cpus } from "os";
const here = import.meta.dir, root = join(here, "../../../..");
const cc = process.env.SAFEC || join(root, "build.noindex/bin/clang");
const sdk = Bun.spawnSync(["xcrun", "--show-sdk-path"]).stdout.toString().trim();
const std = join(root, "safe-c-std");
const arg = (k: string) => process.argv.find((a) => a.startsWith(`--${k}`))?.split("=")[1] ?? (process.argv.includes(`--${k}`) ? "" : undefined);

// Bundles: "//// name" starts each program.
const dir = mkdtempSync(join(tmpdir(), "corpus-"));
const progs: string[] = [];
for (const b of readdirSync(here).filter((f) => f.endsWith(".txt") && !["verdicts.txt", "accepted.txt"].includes(f))) {
  const corpus = b.replace(/\.txt$/, "");
  for (const part of readFileSync(join(here, b), "utf8").split(/^\/\/\/\/ /m).slice(1)) {
    const nl = part.indexOf("\n"), name = `${corpus}/${part.slice(0, nl)}`;
    const f = join(dir, name);
    mkdirSync(dirname(f), { recursive: true });
    writeFileSync(f, part.slice(nl + 1));
    progs.push(name);
  }
}
const only = arg("only");
const todo = only ? progs.filter((p) => p === only) : progs;

type V = "ACCEPT" | "REJECT" | "CRASH";
async function verdict(name: string): Promise<V> {
  const p = Bun.spawn([cc, "-fsafe-c", "-w", "-isysroot", sdk, "-I", std, "-fsyntax-only", join(dir, name)], { stdout: "ignore", stderr: "pipe" });
  const err = await new Response(p.stderr).text();
  const code = await p.exited;
  if (p.signalCode || /Assertion|PLEASE submit|UNREACHABLE/.test(err)) return "CRASH";
  return code === 0 ? "ACCEPT" : "REJECT";
}
const got = new Map<string, V>();
let next = 0;
await Promise.all(Array.from({ length: cpus().length }, async () => {
  while (next < todo.length) { const n = todo[next++]; got.set(n, await verdict(n)); }
}));

const read = (f: string) => { try { return readFileSync(join(here, f), "utf8"); } catch { return ""; } };
const parse = (f: string) => new Map(read(f).split("\n").filter((l) => l && !l.startsWith("#")).map((l) => {
  const [v, ...n] = l.split(" "); return [n.join(" ").split(" # ")[0], v] as [string, string];
}));
const was = parse("verdicts.txt"), allowed = parse("accepted.txt");

// A plain build with ASan: does the program have a memory error? (Same flags as the red-team judges.)
async function asan(name: string): Promise<string> {
  const f = join(dir, name), exe = f + ".asan";
  const b = Bun.spawn(["/usr/bin/clang", "-w", "-g", "-fsanitize=address,null,bounds,alignment", "-fno-sanitize-recover=all", "-isysroot", sdk,
    "-I", std, "-include", join(std, "safec.h"), f, "-o", exe], { stdout: "ignore", stderr: "ignore" });
  if ((await b.exited) !== 0) return "";
  const r = Bun.spawn(["perl", "-e", "alarm 20; exec @ARGV", exe], { env: { ...process.env, ASAN_OPTIONS: "detect_leaks=0:symbolize=0:allocator_may_return_null=1" }, stdin: "ignore", stdout: "ignore", stderr: "pipe" });
  const err = await new Response(r.stderr).text();
  const k = (err.match(/AddressSanitizer: [a-zA-Z-]+|runtime error: [a-z A-Z-]+/) ?? [""])[0];
  return k && !(await safeCBuildSafe(name)) ? k : "";
}
// Safe C stops some memory errors at run time instead of compile time (a bounds or null trap). Such a program is not a hole
// if its Safe C build traps (or is refused at link) at -O0 and at -O2.
// The exact judge: the Safe C build itself, with ASan, at -O0 and -O2. (allocator_may_return_null: an overflowing calloc
// returns NULL, as libc does, instead of ASan reporting it.) The fork has no sanitizer runtime, so its objects
// link against an LLVM whose ASan ABI matches (ASAN_LINK, default Homebrew's). Without one, fall back to traps() above.
const asanLink = process.env.ASAN_LINK || "/opt/homebrew/opt/llvm/bin/clang";
const haveAsanLink = Bun.spawnSync(["test", "-x", asanLink]).exitCode === 0;
async function safeCBuildSafe(name: string): Promise<boolean> {
  if (!haveAsanLink) return traps(name);
  for (const o of ["-O0", "-O2"]) {
    const obj = join(dir, name) + o + ".o", exe = join(dir, name) + o + ".sa";
    const c = Bun.spawn([cc, "-fsafe-c", "-w", "-g", o, "-fsanitize=address", "-isysroot", sdk, "-I", std, "-c", join(dir, name), "-o", obj], { stdout: "ignore", stderr: "ignore" });
    if ((await c.exited) !== 0) return true; // rejected at this level: nothing to run
    const l = Bun.spawn([asanLink, "-fsanitize=address", obj, "-o", exe], { stdout: "ignore", stderr: "ignore" });
    if ((await l.exited) !== 0) return true; // refused at link (the type guard)
    const r = Bun.spawn(["perl", "-e", "alarm 20; exec @ARGV", exe], { env: { ...process.env, ASAN_OPTIONS: "detect_leaks=0:symbolize=0:allocator_may_return_null=1" }, stdin: "ignore", stdout: "ignore", stderr: "pipe" });
    const err = await new Response(r.stderr).text();
    if (/AddressSanitizer/.test(err)) return false;
  }
  return true;
}
async function traps(name: string): Promise<boolean> {
  for (const o of ["-O0", "-O2"]) {
    const exe = join(dir, name) + o;
    const b = Bun.spawn([cc, "-fsafe-c", "-w", o, "-isysroot", sdk, "-I", std, join(dir, name), "-o", exe], { stdout: "ignore", stderr: "ignore" });
    // Refused at link (the type guard on a function declared with another prototype): stopped, nothing to run.
    if ((await b.exited) !== 0) return true;
    const r = Bun.spawn(["perl", "-e", "alarm 20; exec @ARGV", exe], { stdin: "ignore", stdout: "ignore", stderr: "ignore" });
    await r.exited;
    if (!["SIGTRAP", "SIGILL", "SIGABRT"].includes(r.signalCode ?? "")) return false;
  }
  return true;
}
const pool = async <T,>(xs: string[], f: (x: string) => Promise<T>) => {
  const out = new Map<string, T>(); let i = 0;
  await Promise.all(Array.from({ length: cpus().length }, async () => { while (i < xs.length) { const x = xs[i++]; out.set(x, await f(x)); } }));
  return out;
};

const loosened: string[] = [], holes: string[] = [], crashes: string[] = [], tightened: string[] = [], fresh: string[] = [];
for (const n of todo) {
  const v = got.get(n)!, old = was.get(n);
  if (!old) { fresh.push(n); continue; }
  if (v === "CRASH" && old !== "CRASH") crashes.push(n);
  if (v === "ACCEPT" && old !== "ACCEPT") loosened.push(n);
  if (v === "REJECT" && old === "ACCEPT") tightened.push(n);
}
// Each newly accepted program is run under ASan: a memory error makes it a hole, which no sign-off excuses.
const judged = await pool(loosened, asan);
for (const [n, k] of judged) if (k) holes.push(`${n}: ${k}`);
loosened.splice(0, loosened.length, ...loosened.filter((n) => !judged.get(n) && !allowed.has(n)));
// --audit: every accepted program, not only the new ones (slow). Exact when an ASan runtime for the fork is found
// (safeCBuildSafe); otherwise candidates, since a Safe C build can end normally and be safe.
const audit: string[] = [];
if (arg("audit") !== undefined)
  for (const [n, k] of await pool(todo.filter((n) => got.get(n) === "ACCEPT"), asan)) if (k) audit.push(`${n}: ${k}`);
const count = (v: V) => [...got.values()].filter((x) => x === v).length;
console.log(`corpus: ${todo.length} programs: ${count("ACCEPT")} accepted, ${count("REJECT")} rejected, ${count("CRASH")} crash`);
const show = (title: string, xs: string[]) => xs.length && console.log(`${title} (${xs.length}):\n  ${xs.slice(0, 30).join("\n  ")}`);
show("HOLE: newly accepted and ASan finds a memory error", holes);
show("NEWLY ACCEPTED, not in accepted.txt (a maintainer must list each, with a reason)", loosened);
show("NEW CRASH", crashes);
show("CANDIDATE HOLE (audit, triage each): accepted today, ASan finds a memory error, and the Safe C build does not trap", audit);
show("newly rejected (fine if meant; then --update)", tightened);
show("not in verdicts.txt (new programs: --update)", fresh);
const bad = holes.length + loosened.length + crashes.length + audit.length;
if (arg("update") !== undefined && !bad && !only)
  writeFileSync(join(here, "verdicts.txt"), "# Written by check.ts --update. Do not edit by hand.\n" + [...got].sort().map(([n, v]) => `${v} ${n}`).join("\n") + "\n");
process.exit(bad ? 1 : 0);
