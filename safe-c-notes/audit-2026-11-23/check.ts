// Audit gate: every probe in this directory showed memory corruption under
// -fsafe-c with no __unsafe (2026-11-23). A probe is closed when Safe C
// rejects it at compile time or it dies by a compiler trap (SIGTRAP/SIGILL);
// anything else (exit 0, SIGSEGV, SIGBUS, abort) is still open.
//   bun safe-c-notes/audit-2026-11-23/check.ts [--cc path] [--only name]
import { readdirSync, readFileSync } from "fs";
import { join } from "path";
import { spawnSync } from "child_process";

const dir = import.meta.dir;
const args = process.argv.slice(2);
const opt = (n: string) => { const i = args.indexOf("--" + n); return i < 0 ? undefined : args[i + 1]; };
const cc = opt("cc") ?? join(dir, "../../build.noindex/bin/clang");
const only = opt("only");
// --ownership: also enforce the M2 rules, staged behind this flag until the
// ports are annotated for them.
const extra = args.includes("--ownership") ? ["-mllvm", "-safe-c-ownership"] : [];
const sdk = spawnSync("xcrun", ["--show-sdk-path"]).stdout.toString().trim();
const tmp = process.env.TMPDIR ?? "/tmp";

// Probes made of several files; the companions are not run on their own.
const groups: Record<string, string[]> = {
  v05_xtu_a: ["v05_xtu_a.c", "v05_xtu_b.c"],
  v10_extern_arr: ["v10_extern_arr.c", "v10_def.c"],
  w10: ["w10.c", "w10_def.c"],
};
const companions = new Set(["v05_xtu_b.c", "v10_def.c", "w10_def.c"]);
// Probes whose fix is not a trap: checked by looking at the generated code.
const codeChecks: Record<string, RegExp> = {
  // A 4 MB frame: every page must be touched on the way down (stack probes).
  v14_bigframe: /ldr\s+xzr, \[sp\]|orq\s+\$0x0, \(%rsp\)/,
};
// Needs a header directory the audit created outside this folder.
const skip: Record<string, string> = { v07_isystem: "needs its -isystem header directory" };

type Result = { name: string; status: "closed" | "open" | "skipped"; how: string };
const results: Result[] = [];
for (const f of readdirSync(dir).filter((f) => f.endsWith(".c")).sort()) {
  const name = f.replace(/\.c$/, "");
  if (companions.has(f) || (only && name !== only)) continue;
  if (!readFileSync(join(dir, f), "utf8").includes("main(")) continue;
  if (skip[name]) { results.push({ name, status: "skipped", how: skip[name] }); continue; }
  const files = (groups[name] ?? [f]).map((g) => join(dir, g));
  if (codeChecks[name]) {
    const asm = spawnSync(cc, ["-fsafe-c", ...extra, "-O1", "-isysroot", sdk, ...files, "-S", "-o", "-"]);
    const ok = codeChecks[name].test(asm.stdout.toString());
    results.push({ name, status: ok ? "closed" : "open", how: ok ? "probes in code" : "no probes in code" });
    continue;
  }
  const exe = join(tmp, `audit-${name}`);
  const c = spawnSync(cc, ["-fsafe-c", ...extra, "-O1", "-isysroot", sdk, ...files, "-o", exe, "-lpthread"]);
  if (c.status !== 0) {
    const err = c.stderr.toString().split("\n").find((l) => l.includes("error:")) ?? "";
    results.push({ name, status: "closed", how: "rejected: " + err.replace(/^.*error: /, "").slice(0, 90) });
    continue;
  }
  const r = spawnSync(exe, ["1"], { timeout: 20000 });
  const sig = r.signal ?? "";
  if (sig === "SIGTRAP" || sig === "SIGILL")
    results.push({ name, status: "closed", how: "trapped" });
  else
    results.push({ name, status: "open", how: sig ? `died ${sig}` : r.error ? "timed out" : `exit ${r.status}` });
}
const closed = results.filter((r) => r.status === "closed").length;
const open = results.filter((r) => r.status === "open");
for (const r of results) console.log(`${r.status.padEnd(7)} ${r.name.padEnd(18)} ${r.how}`);
console.log(`\n${closed}/${results.length - results.filter((r) => r.status === "skipped").length} closed, ${open.length} open`);
process.exit(open.length ? 1 : 0);
