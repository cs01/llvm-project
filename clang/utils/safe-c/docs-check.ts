// The C in the docs compiles as they say: every example in safe-c-docs/examples.md gives the error printed
// under it (or none, under "Code that compiles"), the README's program builds and runs, and each
// program in try/ gives the errors written under its lines.
//   bun clang/utils/safe-c/docs-check.ts      (SAFEC= another compiler)
import { readFileSync, writeFileSync, mkdtempSync } from "fs";
import { join } from "path";
import { tmpdir } from "os";
const root = join(import.meta.dir, "../../..");
const cc = process.env.SAFEC || join(root, "build.noindex/bin/clang");
const sdk = Bun.spawnSync(["xcrun", "--show-sdk-path"]).stdout.toString().trim();
const dir = mkdtempSync(join(tmpdir(), "docs-"));
const norm = (s: string) => s.replace(/\s+/g, " ").trim();
const bare = (e: string) => norm(e).replace(/ on line \d+/g, "");
let bad = 0, n = 0;
const compile = (name: string, src: string, extra: string[] = []) => {
  const f = join(dir, name);
  writeFileSync(f, src);
  const r = Bun.spawnSync([cc, "-fsafe-c", "-isysroot", sdk, "-I", join(root, "safe-c-std"), "-fsyntax-only", f, ...extra]);
  return (r.stderr.toString().match(/error: .*/g) ?? []).map((e) => e.slice(7));
};
const report = (what: string, ok: boolean, detail: string) => {
  n++;
  if (!ok) { bad++; console.log(`FAIL ${what}: ${detail}`); }
};

// examples.md: the shared declarations, the code that compiles, then each example and its error.
const ex = readFileSync(join(root, "safe-c-docs/examples.md"), "utf8");
const blocks = [...ex.matchAll(/```(c?)\n([\s\S]*?)```/g)].map((m) => ({ c: m[1] === "c", text: m[2], at: m.index! }));
const heads = [...ex.matchAll(/^##+ (.*)$/gm)].map((m) => ({ title: m[1], at: m.index! }));
const under = (at: number) => heads.filter((h) => h.at < at).pop()?.title ?? "";
const pre = "#include <stdlib.h>\n#include <string.h>\n#include <signal.h>\n#include <stdio.h>\n";
const shared = blocks[0].text;
for (let i = 1; i < blocks.length; i++) {
  const b = blocks[i];
  if (!b.c) continue;
  const title = under(b.at);
  const next = blocks[i + 1] && !blocks[i + 1].c ? blocks[i + 1].text : "";
  // Statements, not declarations (the text and container examples): put them in a function.
  // (The text example is statements: it goes in a function.)
  const fragment = /^long total/.test(b.text);
  const body = /#include/.test(b.text)
    ? b.text.replace(/((?:#.*\n)+)/, "$1int f(void) {\n") + "return 0;\n}\n"
    : fragment ? `#include "text.h"\nint f(struct str csv) {\n${b.text}\nreturn (int)total;\n}\n` : b.text;
  const errors = compile(`ex${i}.c`, pre + shared + body);
  if (title === "Code that compiles" || !next.includes("error:")) {
    report(`examples.md: ${title}`, errors.length === 0, errors[0] ?? "");
    continue;
  }
  // The whole message, word for word; only line numbers are left out of the docs.
  const want = norm(next.replace(/^error: /, ""));
  const ok = errors.some((e) => bare(e) === want);
  report(`examples.md: ${title}`, ok, `wanted "${want}", got ${JSON.stringify(errors.map(bare))}`);
}

// README: the program builds and runs, and each error it shows is what the compiler says.
const rd = readFileSync(join(root, "README.md"), "utf8");
const rb = [...rd.matchAll(/```c\n([\s\S]*?)```/g)].map((m) => m[1]);
const prog = rb.find((b) => b.includes("int main"))!;
{
  const f = join(dir, "readme.c");
  writeFileSync(f, prog);
  writeFileSync(join(dir, "people.json"), '[{"name":"Ada"},{"name":"Linus"}]');
  const b = Bun.spawnSync([cc, "-fsafe-c", "-isysroot", sdk, "-I", join(root, "safe-c-std"), f, "-o", join(dir, "readme")]);
  const run = b.exitCode === 0 ? Bun.spawnSync([join(dir, "readme")], { cwd: dir }) : null;
  report("README: the program", !!run && run.stdout.toString() === "Ada\nLinus\n", b.stderr.toString().slice(0, 200) || run?.stdout.toString() || "");
}
const errs = rb.find((b) => b.includes("// error:"))!;
for (const part of errs.split(/\n\s*\n/)) {
  const code = part.split("\n").filter((l) => !l.startsWith("//")).join("\n");
  const want = norm(part.split("\n").filter((l) => l.startsWith("//")).map((l) => l.slice(2)).join(" ")).replace(/^error: /, "");
  const fn = code.includes("person") ? `double f(struct json *j, size_t person) {\n${code}\n}\n` : `int f(struct json *j, struct str text) {\n${code}\n}\n`;
  const got = compile("readme_err.c", `#include "json.h"\n${fn}`);
  report(`README: "${want.slice(0, 40)}..."`, got.some((e) => bare(e) === bare(want)), JSON.stringify(got.map(bare)));
}
// try/: each "// error:" comment is the compiler's error for the line above it, in order, line numbers
// included; overflow.c compiles and stops at the bad read.
for (const name of ["use_after_free", "leak", "double_free", "struct_leaks"]) {
  const f = join(root, "try", name + ".c");
  const lines = readFileSync(f, "utf8").split("\n");
  // Each comment: its text and the code line it sits under (1-based).
  const want: { under: number; text: string }[] = [];
  lines.forEach((l, i) => {
    if (!/^\s*\/\/ error: /.test(l)) return;
    let j = i + 1, text = l.replace(/^\s*\/\/ error: /, "");
    while (j < lines.length && /^\s*\/\/ (?!error:)/.test(lines[j])) text += " " + lines[j++].replace(/^\s*\/\/ /, "");
    want.push({ under: i, text: norm(text) });
  });
  const r = Bun.spawnSync([cc, "-fsafe-c", "-isysroot", sdk, "-fsyntax-only", f]);
  const got = [...r.stderr.toString().matchAll(/^[^\n]*?:(\d+):\d+: error: (.*)$/gm)];
  const ok = got.length === want.length && got.every((g, k) => norm(g[2]) === want[k].text && +g[1] === want[k].under);
  report(`try/${name}.c`, ok, `wanted ${JSON.stringify(want)}, got ${JSON.stringify(got.map((g) => g[1] + ": " + g[2]))}`);
}
{
  const exe = join(dir, "overflow");
  const b = Bun.spawnSync([cc, "-fsafe-c", "-isysroot", sdk, join(root, "try/overflow.c"), "-o", exe]);
  const run = b.exitCode === 0 ? Bun.spawnSync([exe]) : null;
  report("try/overflow.c", run?.signalCode === "SIGTRAP", b.stderr.toString().slice(0, 200) || String(run?.exitCode));
}
console.log(`docs: ${n - bad} of ${n} as the docs say`);
// A parser that stops matching would check fewer; 23 today.
if (n < 23) { console.log(`only ${n} checked, expected at least 23`); bad++; }
process.exit(bad ? 1 : 0);
