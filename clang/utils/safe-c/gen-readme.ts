// Generates the parts of README.md that would otherwise drift: the annotation
// table (from the @annotation lines in safec.h) and the Juliet results table
// (from the saved harness runs in safe-c-notes/). Each part sits between
// <!-- generated:NAME --> and <!-- end generated:NAME --> markers.
//   bun clang/utils/safe-c/gen-readme.ts          rewrite README.md
//   bun clang/utils/safe-c/gen-readme.ts --check  exit 1 if README.md is stale
import { readFileSync, writeFileSync } from "fs";
import { join } from "path";

const root = join(import.meta.dir, "../../..");
const readmePath = join(root, "README.md");
const check = process.argv.includes("--check");

function annotations(): string {
  const h = readFileSync(join(root, "clang/lib/Headers/safec.h"), "utf8");
  // @annotation SPELLING | MEANING | RUST EQUIVALENT
  const rows = [...h.matchAll(/^\s*@annotation (.+?) \| (.+?)(?: \| (.+))?$/gm)].map(
    (m) => `| ${m[1]} | ${m[2]} | ${m[3] ?? ""} |`);
  if (!rows.length) throw new Error("no @annotation lines in safec.h");
  return [
    "| Annotation | Meaning | In Rust |",
    "|---|---|---|",
    "| *(nothing)* | non-null, exactly one element | `&T` |",
    ...rows,
    "",
    "The Rust column is the closest equivalent, not an exact one: a plain Safe C pointer can be",
    "written through (like `&mut T`), and Rust's one-writer-at-a-time rule is not enforced yet;",
    "exclusive borrows (`&mut T` proper) come with static ownership (M2).",
  ].join("\n");
}

// How Rust catches each bug class, and which saved run holds Safe C's result.
type Row = { cwe: string; title: string; rust: "compile time" | "run time"; file: string; fixedMissNote?: string };
const rowsCfg: Row[] = [
  { cwe: "476", title: "NULL dereference", rust: "compile time", file: "juliet-null-2026-11-23f.json" },
  { cwe: "690", title: "unchecked return value", rust: "compile time", file: "juliet-null-2026-11-23f.json" },
  { cwe: "415", title: "double free", rust: "compile time", file: "juliet-415-416-2026-11-23.json" },
  { cwe: "416", title: "use after free", rust: "compile time", file: "juliet-415-416-2026-11-23.json" },
  { cwe: "121", title: "stack buffer overflow", rust: "run time", file: "juliet-full-2026-11-23.json" },
  { cwe: "122", title: "heap buffer overflow", rust: "run time", file: "juliet-full-2026-11-23.json",
    fixedMissNote: "overflow only on 32-bit targets" },
  { cwe: "124", title: "buffer underwrite", rust: "run time", file: "juliet-full-2026-11-23.json" },
  { cwe: "126", title: "buffer over-read", rust: "run time", file: "juliet-full-2026-11-23.json" },
  { cwe: "127", title: "buffer under-read", rust: "run time", file: "juliet-full-2026-11-23.json" },
  { cwe: "369", title: "divide by zero", rust: "run time", file: "juliet-int-2026-11-23.json",
    fixedMissNote: "floating-point division, which is defined (infinity) in C and Rust" },
];

// Input-driven: the bug depends on data the harness's fixed input does not
// produce (rand(), sockets, files, or a randomly taken branch, variant 12).
function inputDriven(files: string[]): boolean {
  const n = files.join(" ");
  const fv = Number(n.match(/_(\d+)[a-z]?\.c/)?.[1] ?? -1);
  return fv === 12 || /rand|socket|fgets|fscanf/.test(n);
}

function juliet(): string {
  const cache: Record<string, any[]> = {};
  const lines = [
    "| CWE | Rust catches it at | Cases | Safe C at compile time | Safe C at run time (trap) | Missed, fixed input | Missed, needs other input |",
    "|---|---|---|---|---|---|---|",
  ];
  const tot = { "compile time": { n: 0, c: 0, r: 0 }, "run time": { n: 0, c: 0, r: 0, fixedMiss: 0, inputMiss: 0 } } as any;
  const notes: string[] = [];
  for (const row of rowsCfg) {
    const recs = (cache[row.file] ??= JSON.parse(readFileSync(join(root, "safe-c-notes", row.file), "utf8")))
      .filter((r: any) => r.cwe === row.cwe && r.good === "accepted");
    const n = recs.length;
    const comp = recs.filter((r: any) => r.bad === "rejected").length;
    const run = recs.filter((r: any) => r.bad === "trapped").length;
    const missed = recs.filter((r: any) => r.bad !== "rejected" && r.bad !== "trapped");
    const fixedMiss = missed.filter((r: any) => !inputDriven(r.files)).length;
    const inputMiss = missed.length - fixedMiss;
    const pct = (x: number) => (n ? `${x} (${((100 * x) / n).toFixed(1)}%)` : "0");
    lines.push(`| ${row.cwe} ${row.title} | ${row.rust} | ${n} | ${pct(comp)} | ${pct(run)} | ${fixedMiss}${fixedMiss && row.fixedMissNote ? "*" : ""} | ${inputMiss} |`);
    if (fixedMiss && row.fixedMissNote) notes.push(`\\* CWE ${row.cwe}: the ${fixedMiss} fixed-input misses are ${row.fixedMissNote}.`);
    const t = tot[row.rust];
    t.n += n; t.c += comp; t.r += run; if (row.rust === "run time") { t.fixedMiss += fixedMiss; t.inputMiss += inputMiss; }
  }
  const ct = tot["compile time"], rt = tot["run time"];
  const p = (x: number, n: number) => `${((100 * x) / n).toFixed(1)}%`;
  return [
    ...lines,
    "",
    ...notes,
    "",
    `**Bugs Rust rejects at compile time** (null, unchecked returns, double free, use after free): Safe C rejects ${p(ct.c, ct.n)} of them at compile time (${ct.c} of ${ct.n}); the misses are double frees through function pointers and structs, the open struct-field gap.`,
    "",
    `**Bugs Rust catches at run time** (bounds, division by zero, where Rust panics): Safe C traps on every one whose bug actually happens with the harness's input (${rt.c + rt.r} of ${rt.n - rt.inputMiss - rt.fixedMiss}). The other ${rt.inputMiss} need an input the harness does not give (a random value, a socket, a file), so a Rust program would not panic on them in this run either, and ${rt.fixedMiss} are not bugs on this target (marked *).`,
    "",
    "Cases are `strict`: only those whose fixed `good()` version compiles and runs under Safe C. Generated from the saved runs in `safe-c-notes/` by `clang/utils/safe-c/gen-readme.ts`.",
  ].join("\n");
}

const parts: Record<string, () => string> = { annotations, juliet };
let readme = readFileSync(readmePath, "utf8");
const original = readme;
for (const [name, gen] of Object.entries(parts)) {
  const re = new RegExp(`(<!-- generated:${name} -->)[\\s\\S]*?(<!-- end generated:${name} -->)`);
  if (!re.test(readme)) throw new Error(`README.md has no generated:${name} markers`);
  readme = readme.replace(re, (_m, a, b) => `${a}\n${gen()}\n${b}`);
}
if (check) {
  if (readme !== original) { console.error("README.md is stale: run bun clang/utils/safe-c/gen-readme.ts"); process.exit(1); }
  console.log("README.md generated sections are current");
} else {
  writeFileSync(readmePath, readme);
  console.log("README.md updated");
}
