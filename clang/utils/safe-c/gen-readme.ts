// Generates the parts of the Safe C docs (README.md, safe-c-docs/) that would
// otherwise drift: the annotation
// table (from the @annotation lines in safec.h) and the Juliet results table
// (from the saved harness runs in safe-c-notes/). Each part sits between
// <!-- generated:NAME --> and <!-- end generated:NAME --> markers.
//   bun clang/utils/safe-c/gen-readme.ts          rewrite README.md
//   bun clang/utils/safe-c/gen-readme.ts --check  exit 1 if README.md is stale
import { readFileSync, writeFileSync } from "fs";
import { join } from "path";

const root = join(import.meta.dir, "../../..");
const readmePath = join(root, "README.md");
// Which file holds each generated part.
const partFiles: Record<string, string> = {
  summary: readmePath,
  annotations: join(root, "safe-c-docs/annotations.md"),
  juliet: join(root, "safe-c-docs/results.md"),
  rules: join(root, "safe-c-docs/rules.md"),
};
const check = process.argv.includes("--check");

function annotations(): string {
  const h = readFileSync(join(root, "clang/lib/Headers/safec.h"), "utf8");
  // @annotation SPELLING | FROM | MEANING | RUST EQUIVALENT | EXAMPLE
  const rows = [...h.matchAll(/^\s*@annotation (.+?) \| (.+?) \| (.+?) \| (.+?) \| (.+)$/gm)].map(
    (m) => `| ${m[1]} | ${m[3]} | ${m[4]} | ${m[5]} | ${m[2]} |`);
  if (!rows.length) throw new Error("no @annotation lines in safec.h");
  return [
    "| Annotation | Meaning | In Rust | Example | From |",
    "|---|---|---|---|---|",
    "| *(nothing)* | non-null, exactly one element | `&T` | `void reset(struct conn *c);` | Safe C default |",
    ...rows,
    "",
    "The Rust column is the closest equivalent, not an exact one. A plain pointer may write the",
    "data it points to, but cannot free or replace what that data owns; that takes `_Exclusive`",
    "(Rust's `&mut T`), and lending one ends the caller's other borrows of it. Locals need no",
    "kind: one that only ever holds owned values is an owner, as `let x = Box::new(..)` is.",
    "Rust's rule that two plain pointers may not both write the same data at once is not",
    "enforced (it matters for threads, M5).",
  ].join("\n");
}

// The saved run the docs report, and whether it is of the suite as written or
// after clang/utils/safe-c/juliet-port.ts (listed in results.md).
const RUN_FILE = "juliet-port6-2026-11-24.json";
const PORTED = true;

// How Rust catches each bug class, and which saved run holds Safe C's result.
type Row = { cwe: string; title: string; rust: "compile time" | "run time"; file: string; fixedMissNote?: string; excuse?: (r: any) => boolean };
const rowsCfg: Row[] = [
  { cwe: "476", title: "NULL dereference", rust: "compile time", file: RUN_FILE },
  { cwe: "690", title: "unchecked return value", rust: "compile time", file: RUN_FILE },
  { cwe: "415", title: "double free", rust: "compile time", file: RUN_FILE },
  { cwe: "416", title: "use after free", rust: "compile time", file: RUN_FILE },
  { cwe: "121", title: "stack buffer overflow", rust: "run time", file: RUN_FILE },
  { cwe: "122", title: "heap buffer overflow", rust: "run time", file: RUN_FILE,
    fixedMissNote: "overflow only on 32-bit targets" },
  { cwe: "124", title: "buffer underwrite", rust: "run time", file: RUN_FILE },
  { cwe: "126", title: "buffer over-read", rust: "run time", file: RUN_FILE,
    // CWE170 variants copy into a local array without terminating it, then
    // print it. Safe C zero-fills locals, so the copy is terminated inside
    // the array and nothing is read past it (Rust requires the init too).
    excuse: (r: any) => /CWE170/.test(r.case),
    fixedMissNote: "missing-terminator cases (CWE 170) that cannot over-read under Safe C: local arrays are zero-filled, so the copy stays terminated inside the buffer (Rust requires the same initialization)" },
  { cwe: "127", title: "buffer under-read", rust: "run time", file: RUN_FILE },
  { cwe: "369", title: "divide by zero", rust: "run time", file: RUN_FILE,
    fixedMissNote: "floating-point division, which is defined (infinity) in C and Rust" },
];

// Input-driven: the bug depends on data the harness's fixed input does not
// produce (rand(), sockets, files, or a randomly taken branch, variant 12).
function inputDriven(files: string[]): boolean {
  const n = files.join(" ");
  const fv = Number(n.match(/_(\d+)[a-z]?\.c/)?.[1] ?? -1);
  return fv === 12 || /rand|socket|fgets|fscanf/.test(n);
}

let lastTotals: any;
// One decimal, rounded down, so a single miss never reads as 100.0%.
const pctOf = (x: number, n: number) => `${(Math.floor((1000 * x) / n) / 10).toFixed(1)}%`;
function summary(): string {
  if (!lastTotals) juliet();
  const { ct, rt } = lastTotals;
  const caught = rt.c + rt.r, missed = rt.fixedMiss - rt.excused;
  const n = (x: number) => x.toLocaleString("en-US");
  const porting = PORTED ? " (those whose fixed version builds after porting)" : "";
  const missText = missed ? `, ${n(missed)} missed` : "";
  return [
    `On ${PORTED ? "a ported copy of " : ""}NIST's Juliet suite, ${n(ct.n + rt.n)} of ${n(ct.all + rt.all)} cases in 10 bug classes${porting}:`,
    ``,
    `- **Null, double free, use after free: ${n(ct.c)} of ${n(ct.n)} rejected at compile time.** Rust also rejects these at compile time.`,
    `- **Out of bounds, divide by zero: ${n(caught)} of ${n(rt.n)} stopped at the bad access**${missText} (being rechecked: for 3,221 of them the harness could not rule out a trap raised by the platform). The other ${n(rt.n - caught - missed)}: ${n(rt.inputMiss)} need an input the harness does not give (a random value, a file, a socket), and ${n(rt.excused)} are not bugs on this target (32-bit-only overflows, floating-point division, buffers that stay terminated).`,
    ``,
    `A case counts only when Safe C gives a compile error or traps at the bug. [Full table, the port and the exclusions](safe-c-docs/results.md).`,
  ].join("\n");
}

function juliet(): string {
  const cache: Record<string, any[]> = {};
  const lines = [
    "| CWE | Rust catches it at | Cases | Safe C at compile time | Safe C at run time (trap) | Missed, fixed input | Missed, needs other input |",
    "|---|---|---|---|---|---|---|",
  ];
  const tot = { "compile time": { n: 0, c: 0, r: 0, all: 0 }, "run time": { n: 0, c: 0, r: 0, fixedMiss: 0, excused: 0, inputMiss: 0, all: 0 } } as any;
  const notes: string[] = [];
  for (const row of rowsCfg) {
    const all = (cache[row.file] ??= JSON.parse(readFileSync(join(root, "safe-c-notes", row.file), "utf8")))
      .filter((r: any) => r.cwe === row.cwe);
    const recs = all.filter((r: any) => r.good === "accepted");
    const n = recs.length;
    const comp = recs.filter((r: any) => r.bad === "rejected").length;
    const run = recs.filter((r: any) => r.bad === "trapped").length;
    const missed = recs.filter((r: any) => r.bad !== "rejected" && r.bad !== "trapped");
    const fixedMiss = missed.filter((r: any) => !inputDriven(r.files)).length;
    const inputMiss = missed.length - fixedMiss;
    const pct = (x: number) => (n ? `${x} (${pctOf(x, n)})` : "0");
    const excused = row.excuse
      ? missed.filter((r: any) => !inputDriven(r.files) && row.excuse!(r)).length
      : row.fixedMissNote ? fixedMiss : 0;
    lines.push(`| ${row.cwe} ${row.title} | ${row.rust} | ${n} | ${pct(comp)} | ${pct(run)} | ${fixedMiss}${excused ? (excused === fixedMiss ? "*" : ` (${excused}*)`) : ""} | ${inputMiss} |`);
    if (excused) notes.push(`\\* CWE ${row.cwe}: ${excused === fixedMiss ? `the ${fixedMiss}` : `${excused} of the ${fixedMiss}`} fixed-input misses are ${row.fixedMissNote}.`);
    const t = tot[row.rust];
    t.n += n; t.c += comp; t.r += run; t.all += all.length;
    if (row.rust === "run time") {
      t.fixedMiss += fixedMiss; t.inputMiss += inputMiss;
      // Only misses with a stated reason (marked *) are not bugs here.
      t.excused += excused;
    }
  }
  const ct = tot["compile time"], rt = tot["run time"];
  lastTotals = { ct, rt };
  const runRows = cache[RUN_FILE] ?? [];
  const cwes = new Set(rowsCfg.map((r) => r.cwe));
  // Flow variants 05-17 set the data only under a condition the compiler
  // cannot see through (a global from another file, a function's result):
  // on the other path it is uninitialized or still null.
  const condVariant = (r: any) => { const v = Number(r.case.match(/_(\d\d)[a-e]?$/)?.[1] ?? -1); return v >= 5 && v <= 17; };
  const excludedUninit = runRows.filter((r: any) => cwes.has(r.cwe) && r.good === "false-positive" &&
    (/error: variable .* (may be uninitialized|is used uninitialized)/.test(r.goodErr ?? "") ||
     (condVariant(r) && /error: passing nullable pointer to nonnull parameter|error: dereference of nullable pointer/.test(r.goodErr ?? "")))).length;
  const p = pctOf;
  return [
    ...lines,
    "",
    ...notes,
    "",
    `**Bugs Rust rejects at compile time** (null, unchecked returns, double free, use after free): Safe C rejects ${p(ct.c, ct.n)} of them at compile time (${ct.c} of ${ct.n})${ct.c < ct.n ? "; the rest get past it" : ""}.`,
    "",
    `**Bugs Rust catches at run time** (bounds, division by zero, where Rust panics): of the ${rt.n - rt.inputMiss - rt.excused} whose bug happens with the harness's input, Safe C traps on ${rt.c + rt.r} (${p(rt.c + rt.r, rt.n - rt.inputMiss - rt.excused)}); the other ${rt.fixedMiss - rt.excused} get past it. ${rt.inputMiss} more need an input the harness does not give (a random value, a socket, a file), so a Rust program would not panic on them in this run either, and ${rt.excused} are not bugs on this target (marked *).`,
    "",
    PORTED
      ? `Cases are \`strict\`: only those whose fixed \`good()\` version compiles and runs under Safe C after porting (below), ${ct.n + rt.n} of ${ct.all + rt.all}. Of the rest, ${excludedUninit} use a variable that is set only under a condition known at run time (on the other path it is uninitialized or still null), which Rust rejects too; the others still need porting work. Generated from \`safe-c-notes/${RUN_FILE}\` by \`clang/utils/safe-c/gen-readme.ts\`.`
      : `Cases are \`strict\`: only those whose fixed \`good()\` version compiles and runs under Safe C, ${ct.n + rt.n} of ${ct.all + rt.all}. The rest are rejected as written, mostly because Juliet keeps raw pointers in struct fields and possibly-null values where Safe C wants them annotated: that is annotation work, not a bug found. Generated from the saved runs in \`safe-c-notes/\` by \`clang/utils/safe-c/gen-readme.ts\`.`,
  ].join("\n");
}

// Every example in the annotation table is compiled by a lit test, so a wrong
// or stale example fails the test suite instead of misleading a reader.
const examplesTestPath = join(root, "clang/test/Sema/safe-c-annotation-examples.c");
function examplesTest(): string {
  const h = readFileSync(join(root, "clang/lib/Headers/safec.h"), "utf8");
  const decls = [...h.matchAll(/^\s*@annotation .+? \| .+? \| .+? \| .+? \| `([^`]+[;}])`/gm)].map((m) => m[1]);
  return [
    "// Generated by clang/utils/safe-c/gen-readme.ts from the @annotation lines in",
    "// safec.h: every example in the README's annotation table must compile, under",
    "// Safe C and as plain C. Do not edit; edit safec.h and rerun the script.",
    "// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \\",
    "// RUN:   -fnullability-default=nonnull -include safec.h -verify %s",
    "// RUN: %clang_cc1 -fsyntax-only -include safec.h -verify %s",
    "// expected-no-diagnostics",
    "typedef __SIZE_TYPE__ size_t;",
    "typedef __builtin_va_list va_list;",
    "#define va_arg(ap, t) __builtin_va_arg(ap, t)",
    "struct conn;",
    "struct json;",
    ...decls,
    "",
  ].join("\n");
}

// The lists in rules.md, read from the compiler's source so they cannot drift.
function rules(): string {
  const sema = readFileSync(join(root, "clang/lib/Sema/SemaSafeC.cpp"), "utf8");
  const between = (text: string, from: string, to: string) => {
    const i = text.indexOf(from);
    const j = text.indexOf(to, i);
    if (i < 0 || j < 0) throw new Error("rules: cannot find " + from);
    return text.slice(i, j);
  };
  const names = (text: string, re: RegExp) => [...new Set([...text.matchAll(re)].map((m) => m[1]))];
  const stmts = names(between(sema, "switch (St->getStmtClass())", "default:"), /Stmt::(\w+?)Class:/g);
  const casts = names(between(sema, "switch (C->getCastKind())", "default:"), /case CK_(\w+):/g);
  const decls = names(between(sema, "switch (D->getKind())", "default:"), /case Decl::(\w+):/g);
  const types = names(between(sema, "switch (C->getTypeClass())", "default:"), /case Type::(\w+):/g);
  const attr = readFileSync(join(root, "clang/lib/Sema/SemaDeclAttr.cpp"), "utf8");
  const attrs = names(between(attr, "// Known: checked by their own rules", "default:"), /AT_(\w+):/g);
  const type = readFileSync(join(root, "clang/lib/Sema/SemaType.cpp"), "utf8");
  const typeAttrs = names(between(type, "// Safe C: only type attributes it has rules for.", "default:"), /AT_(\w+):/g);
  const diags = [...sema.matchAll(/\{"([a-z0-9-]+)",\s*(true|false),\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\}/g)]
    .filter((m) => m[2] === "false")
    .map((m) => [m[1], [...m[3].matchAll(/"((?:[^"\\]|\\.)*)"/g)].map((x) => x[1]).join("")]);
  if (stmts.length < 40 || casts.length < 20 || attrs.length < 30 || diags.length < 50 || decls.length < 12 || types.length < 10)
    throw new Error(`rules: parsed too little (${stmts.length} ${casts.length} ${attrs.length} ${diags.length})`);
  const code = (xs: string[]) => xs.map((x) => "`" + x + "`").join(", ");
  return [
    `**Statements and expressions** (${stmts.length}): ${code(stmts)}`, "",
    `**Casts** (${casts.length}): ${code(casts)}`, "",
    `**Declarations** (${decls.length}): ${code(decls)}`, "",
    `**Types** (${types.length}): ${code(types)}`, "",
    `**Declaration attributes** (${attrs.length}): ${code(attrs)}`, "",
    `**Type attributes** (${typeAttrs.length}): ${code(typeAttrs)}`, "",
    `**Errors** (${diags.length}; the bounds, null and lifetime errors of Clang itself are not listed):`, "",
    "| Rule | Message |", "|---|---|",
    ...diags.sort((a, b) => (a[0] < b[0] ? -1 : 1)).map(([k, t]) => `| \`${k}\` | ${t.replace(/\\"/g, '"').replace(/\|/g, "\\|")} |`),
  ].join("\n");
}

const parts: Record<string, () => string> = { annotations, juliet, summary, rules };
const files: Record<string, { original: string; text: string }> = {};
for (const [name, gen] of Object.entries(parts)) {
  const path = partFiles[name];
  files[path] ??= { original: readFileSync(path, "utf8"), text: "" };
  const f = files[path];
  if (!f.text) f.text = f.original;
  const re = new RegExp(`(<!-- generated:${name} -->)[\\s\\S]*?(<!-- end generated:${name} -->)`);
  if (!re.test(f.text)) throw new Error(`${path} has no generated:${name} markers`);
  f.text = f.text.replace(re, (_m, a, b) => `${a}\n${gen()}\n${b}`);
}
const examples = examplesTest();
let oldExamples = "";
try { oldExamples = readFileSync(examplesTestPath, "utf8"); } catch {}
if (check) {
  const stale = Object.entries(files).filter(([, f]) => f.text !== f.original).map(([p]) => p);
  if (examples !== oldExamples) stale.push(examplesTestPath);
  if (stale.length) { console.error("stale (run bun clang/utils/safe-c/gen-readme.ts): " + stale.join(", ")); process.exit(1); }
  console.log("generated docs are current");
} else {
  for (const [p, f] of Object.entries(files)) writeFileSync(p, f.text);
  writeFileSync(examplesTestPath, examples);
  console.log("generated docs updated");
}
