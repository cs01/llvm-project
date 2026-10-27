// Soundness check for -fsemantic-deps + clang-semindex, in seconds: for each
// mutation of lib.h, every tu_*.cpp is compiled before and after the edit
// against a PCH of lib.h, and whichever ones the tracker would keep must come
// out byte-identical. A kept object that changed, or whose compile now fails,
// is a false skip: a bug.
//
// usage: bun fuzz.ts <bin dir> [mutation-name]
//        bun fuzz.ts <bin dir> --random <count> <seed>   (failing edits saved)
import { execFileSync, spawnSync } from "child_process";
import { cpSync, existsSync, mkdirSync, readFileSync, readdirSync, realpathSync, rmSync, writeFileSync } from "fs";
import { join } from "path";
import { semdiff } from "../semdiff.ts";

const [bin, only] = process.argv.slice(2).filter((a) => a !== "--random");
const here = import.meta.dir;
const sdk = execFileSync("xcrun", ["--show-sdk-path"]).toString().trim();
const work = join(process.env.TMPDIR ?? "/tmp", "semdeps-fuzz");
mkdirSync(work, { recursive: true });
const tus = readdirSync(here).filter((f) => /^tu_.*\.cpp$/.test(f));
const base = readFileSync(join(here, "lib.h"), "utf8");
const flags = ["-isysroot", sdk, "-std=c++17", "-O2", "-fno-exceptions"];

type Mutation = { name: string; from: string; to: string };
const m = (name: string, from: string, to: string): Mutation => ({ name, from, to });
const mutations: Mutation[] = [
  m("member-added-unused", "int sum() const", "int extra() const { return 9; }\n  int sum() const"),
  m("member-body-changed", "return x + y;", "return y + x + 0 * x;"),
  m("member-removed-used", "  int sum() const { return x + y; }\n", ""),
  m("field-added", "int x, y;", "int x, y, z;"),
  m("constexpr-chain", "Base = 1;", "Base = 5;"),
  m("macro-used-changed", "#define SCALE 3", "#define SCALE 4"),
  m("macro-unused-changed", "#define UNUSED_MACRO 1", "#define UNUSED_MACRO 2"),
  m("conditional-around-decl", "inline int unused(int x) { return x + 7; }",
    "#if UNUSED_MACRO\ninline int unused(int x) { return x + 7; }\n#endif"),
  m("overload-declaration-gets-body", "int overloaded(int);", "inline int overloaded(int x) { return x * 3; }"),
  m("overload-added-not-chosen", "int overloaded(int);", "int overloaded(int);\ninline int overloaded(char c) { return c; }"),
  m("overload-added-chosen", "inline int overloaded(long x)",
    "inline int overloaded(unsigned x) { return 5; }\ninline int overloaded(long x)"),
  m("parameter-type-changed", "inline int useCount(Count c)", "inline int useCount(long c)"),
  m("nodiscard-added", "inline int twice(int x) {", "[[nodiscard]] inline int twice(int x) {"),
  m("virtual-added", "virtual int area() const { return 0; }",
    "virtual int perimeter() const { return 0; }\n  virtual int area() const { return 0; }"),
  m("default-member-init", "int w = 1, h = 1;", "int w = 2, h = 1;"),
  m("base-class-added", "struct Point {", "struct Tagged {\n  int tag = 7;\n};\nstruct Point : Tagged {"),
  m("hidden-friend-added", "  int scaled() const", "  friend int operator+(Point a, Point b) { return a.x + b.x; }\n  int scaled() const"),
  m("conversion-added", "  int scaled() const", "  operator long() const { return x; }\n  int scaled() const"),
  m("using-declaration-added", "struct Box : Shape {", "struct Box : Shape {\n  using Shape::Shape;"),
  m("partial-specialization-added", "template <typename T> T identity",
    "template <typename T> struct Holder<T *> {\n  T *value;\n  T *get() const { return nullptr; }\n};\ntemplate <typename T> T identity"),
  m("explicit-specialization-added", "enum Color",
    "template <> inline long identity<long>(long v) { return v + 1; }\nenum Color"),
  m("template-default-argument", "template <typename T> struct Holder", "template <typename T = int> struct Holder"),
  m("enumerator-added", "Red, Green", "Red, Green, Blue"),
  m("enumerator-renumbered", "Red, Green", "Red = 2, Green"),
  m("typedef-changed", "using Count = int;", "using Count = long;"),
  m("typedef-chain", "using Count = int;", "using Width = int;\nusing Count = Width;"),
  m("static-member-changed", "struct Box : Shape {", "struct Box : Shape {\n  static constexpr int Sides = 4;"),
  m("opaque-defined", "struct Opaque;", "struct Opaque {\n  int q;\n};"),
  m("adl-body-changed", "inline int find(Tag) { return 1; }", "inline int find(Tag) { return 3; }"),
  m("adl-worse-candidate-added", "template <typename T> int callFind",
    "namespace adl {\ninline int find(Tag, int) { return 2; }\n} // namespace adl\ntemplate <typename T> int callFind"),
  m("inline-namespace-added", "namespace adl {\nstruct Tag {};", "namespace adl {\ninline namespace v2 {}\nstruct Tag {};"),
  m("line-shift-above-assert", "#define SCALE 3", "\n\n#define SCALE 3"),
  m("unrelated-function-added", "} // namespace lib", "inline int brandNew() { return 4; }\n} // namespace lib"),
  // A non-inline variable defined in a header is emitted by every includer.
  m("namespace-variable-added", "int overloaded(int);", "int overloaded(int);\nint everyoneEmitsThis = 3;"),
  m("explicit-instantiation-added", "enum Color", "template struct Holder<char>;\nenum Color"),
  m("unused-function-renamed", "inline int unused(int x)", "inline int unusedRenamed(int x)"),
];

const run = (args: string[]) => spawnSync(args[0], args.slice(1), { cwd: work, encoding: "utf8" }).status === 0;
/// Builds the PCH and every TU against `header`; a TU that fails has no object.
const build = (header: string, out: string) => {
  writeFileSync(join(work, "lib.h"), header);
  rmSync(join(work, out), { recursive: true, force: true });
  mkdirSync(join(work, out), { recursive: true });
  if (!run([`${bin}/clang++`, ...flags, "-x", "c++-header", "lib.h", "-o", "lib.pch"])) return false;
  for (const t of tus)
    run([`${bin}/clang++`, ...flags, "-Xclang", "-include-pch", "-Xclang", "lib.pch", "-Xclang", "-include",
      "-Xclang", "lib.h", "-Xclang", "-fsemantic-deps", "-c", t, "-o", `${out}/${t}.o`]);
  return true;
};
const object = (out: string, t: string) => {
  const p = join(work, out, `${t}.o`);
  return existsSync(p) ? readFileSync(p) : null;
};
const index = (contents?: string) => {
  const args = ["-p", work, "--header", join(work, "lib.h"), ...(contents ? ["--contents", contents] : []), join(work, tus[0])];
  return JSON.parse(spawnSync(`${bin}/clang-semindex`, args, { encoding: "utf8", maxBuffer: 1 << 28 }).stdout);
};

for (const t of tus) cpSync(join(here, t), join(work, t));
writeFileSync(join(work, "compile_commands.json"), JSON.stringify(tus.map((t) => ({
  directory: work, file: join(work, t), output: `${t}.o`, command: ["clang++", ...flags, "-c", t].join(" "),
}))));

// Random edits, composed from operators that keep the header mostly valid.
// A seeded generator, so a failing case can be replayed by its number.
function rng(seed: number) {
  return () => ((seed = (seed * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff);
}
function randomEdit(header: string, rand: () => number): string {
  const pick = <T,>(xs: T[]) => xs[Math.floor(rand() * xs.length)];
  const lines = header.split("\n");
  // Lines inside a struct body ("  ..." members) and at namespace scope.
  const member = lines.map((l, i) => [l, i] as const).filter(([l]) => /^  (int|virtual|T|static)/.test(l)).map(([, i]) => i);
  const scope = lines.map((l, i) => [l, i] as const).filter(([l]) => /^(inline|template|struct|enum|using|int|constexpr)/.test(l)).map(([, i]) => i);
  const fresh = () => `r${Math.floor(rand() * 1e6)}`;
  const ops: (() => void)[] = [
    () => { // change a numeric literal
      const i = pick(lines.map((l, i) => [l, i] as const).filter(([l]) => /\b\d+\b/.test(l) && !l.startsWith("#include")).map(([, i]) => i));
      if (i !== undefined) lines[i] = lines[i].replace(/\b(\d+)\b/, (d) => String(Number(d) + 1 + Math.floor(rand() * 5)));
    },
    () => { // perturb a return expression
      const i = pick(lines.map((l, i) => [l, i] as const).filter(([l]) => /return [^;]+;/.test(l) && !/nullptr|\(int\)|value;/.test(l)).map(([, i]) => i));
      if (i !== undefined) lines[i] = lines[i].replace(/return ([^;]+);/, (_, e) => `return (${e}) + 1;`);
    },
    () => lines.splice(Math.floor(rand() * lines.length), 0, ""), // shift lines
    () => { // change a macro value
      const i = pick(lines.map((l, i) => [l, i] as const).filter(([l]) => /^#define \w+ \d+$/.test(l)).map(([, i]) => i));
      if (i !== undefined) lines[i] = lines[i].replace(/\d+$/, (d) => String(Number(d) + 1));
    },
    () => { const i = pick(member); if (i !== undefined) lines.splice(i, 0, `  int ${fresh()} = 0;`); }, // add a field
    () => { const i = pick(member); if (i !== undefined) lines.splice(i, 0, `  int ${fresh()}() const { return 1; }`); },
    () => { const i = pick(scope); if (i !== undefined) lines.splice(i, 0, `inline int ${fresh()}(int v) { return v; }`); },
    () => { const i = pick(scope); if (i !== undefined) lines.splice(i, 0, `inline int overloaded(${pick(["short", "unsigned", "char", "double"])} v) { return 7; }`); },
    () => { // drop inline: the definition is then emitted by every includer
      const i = pick(lines.map((l, i) => [l, i] as const).filter(([l]) => /^inline /.test(l)).map(([, i]) => i));
      if (i !== undefined) lines[i] = lines[i].replace(/^inline /, "");
    },
    () => { const i = pick(scope); if (i !== undefined) lines.splice(i, 0, `template struct Holder<${pick(["char", "short", "double"])}>;`); },
    () => { const i = pick(scope); if (i !== undefined) lines.splice(i, 0, `static int ${fresh()} = twice(2);`); },
    () => { // add a default argument
      const i = pick(lines.map((l, i) => [l, i] as const).filter(([l]) => /^inline int \w+\(int \w+\)/.test(l)).map(([, i]) => i));
      if (i !== undefined) lines[i] = lines[i].replace(/\(int (\w+)\)/, "(int $1 = 4)");
    },
    () => { // toggle virtual
      const i = pick(lines.map((l, i) => [l, i] as const).filter(([l]) => /^  (virtual )?int \w+\(\) const \{/.test(l) && !/override/.test(l)).map(([, i]) => i));
      if (i !== undefined) lines[i] = lines[i].startsWith("  virtual ") ? lines[i].replace("virtual ", "") : lines[i].replace(/^  /, "  virtual ");
    },
    () => { // add a base class
      const i = pick(lines.map((l, i) => [l, i] as const).filter(([l]) => /^struct \w+ \{$/.test(l)).map(([, i]) => i));
      if (i !== undefined) { const b = fresh(); lines[i] = lines[i].replace(/ \{$/, ` : ${b} {`); lines.splice(i, 0, `struct ${b} { int ${b}v = 1; };`); }
    },
    () => { const i = pick(scope); if (i !== undefined) lines.splice(i, 0, "using namespace adl;"); },
    () => { const i = pick(scope); if (i !== undefined && i > lines.findIndex((l) => l.startsWith("struct Point"))) lines.splice(i, 0, "inline int operator+(Point a, Point b) { return a.x * b.y; }"); },
    () => { // noexcept on an inline function
      const i = pick(lines.map((l, i) => [l, i] as const).filter(([l]) => /^inline int \w+\([^)]*\) \{/.test(l)).map(([, i]) => i));
      if (i !== undefined) lines[i] = lines[i].replace(/\) \{/, ") noexcept {");
    },
    () => { // swap two adjacent one-line declarations
      const i = pick(lines.map((l, i) => [l, i] as const).filter(([l], k) => /;$|\}$/.test(l) && /^(inline|constexpr|using)/.test(l) &&
        /^(inline|constexpr|using)/.test(lines[k + 1] ?? "") && /;$|\}$/.test(lines[k + 1] ?? "")).map(([, i]) => i));
      if (i !== undefined) [lines[i], lines[i + 1]] = [lines[i + 1], lines[i]];
    },
    () => { // flip a type
      const i = pick(lines.map((l, i) => [l, i] as const).filter(([l]) => /\bint\b/.test(l) && !/^#/.test(l)).map(([, i]) => i));
      if (i !== undefined) lines[i] = lines[i].replace(/\bint\b/, "long");
    },
  ];
  const n = 1 + Math.floor(rand() * 3);
  for (let k = 0; k < n; k++) pick(ops)();
  return lines.join("\n");
}

let falseSkips = 0, kept = 0, total = 0, broken = 0;
let beforeBuilt = false;
function check(name: string, edited: string, quiet = false) {
  if (!beforeBuilt) {
    if (!build(base, "before")) throw new Error("lib.h itself does not build");
    beforeBuilt = true;
  }
  writeFileSync(join(work, "old.h"), base);
  writeFileSync(join(work, "lib.h"), edited);
  const diff = semdiff(index(join(work, "old.h")), index());
  const names = new Set(diff.names), bodies = new Set([...diff.bodies].map((b) => b + "()"));
  const header = realpathSync(join(work, "lib.h"));
  const verdicts = new Map<string, boolean>();
  for (const t of tus) {
    const lines = readFileSync(join(work, "before", `${t}.o.sdeps`), "utf8").split("\n");
    verdicts.set(t, diff.all !== null || lines.includes("*") || lines.includes(`#file ${header}`) ||
      lines.some((n) => names.has(n) || bodies.has(n)));
  }
  const pchBuilt = build(edited, "after");
  if (!pchBuilt) broken++;
  const report: string[] = [];
  let failed = false;
  for (const t of tus) {
    total++;
    const after = pchBuilt ? object("after", t) : null;
    const changed = !after || !object("before", t)!.equals(after);
    const recompile = verdicts.get(t)!;
    if (!recompile) kept++;
    const what = !after ? "now fails to compile" : changed ? "changed" : "";
    if (!recompile && changed) { falseSkips++; failed = true; report.push(`FALSE SKIP ${t} (${what})`); }
    else report.push(`${recompile ? "rebuild" : "keep   "} ${t}${what ? ` (${what})` : ""}`);
  }
  if (!quiet || failed) {
    console.log(`${name}${diff.all ? `  [all: ${diff.all}]` : ""}\n  ${report.join("\n  ")}`);
    if (failed) writeFileSync(join(work, `${name}.h`), edited);
  }
}

const random = process.argv.indexOf("--random");
if (random >= 0) {
  const count = Number(process.argv[random + 1] ?? 100), seed = Number(process.argv[random + 2] ?? 1);
  const rand = rng(seed);
  for (let i = 0; i < count; i++) {
    const edited = randomEdit(base, rand);
    if (edited !== base) check(`random-${seed}-${i}`, edited, true);
  }
} else {
  for (const mu of mutations) {
    if (only && mu.name !== only) continue;
    if (!base.includes(mu.from)) throw new Error(`${mu.name}: "${mu.from}" not in lib.h`);
    check(mu.name, base.replace(mu.from, mu.to));
  }
}
console.log(`\n${total / tus.length} edits (${broken} broke lib.h), ${total} verdicts, ${kept} kept, FALSE SKIPS ${falseSkips}`);
process.exit(falseSkips ? 1 : 0);
