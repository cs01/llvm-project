// The C in the docs compiles as they say: every example in safe-c-docs/examples.md gives the error printed
// under it (or none, under "Code that compiles"), and the README's program builds and runs.
//   bun clang/utils/safe-c/docs-check.ts      (SAFEC= another compiler)
import { readFileSync, writeFileSync, mkdtempSync } from "fs";
import { join } from "path";
import { tmpdir } from "os";
const root = join(import.meta.dir, "../../..");
const cc = process.env.SAFEC || join(root, "build.noindex/bin/clang");
const sdk = Bun.spawnSync(["xcrun", "--show-sdk-path"]).stdout.toString().trim();
const dir = mkdtempSync(join(tmpdir(), "docs-"));
const norm = (s: string) => s.replace(/\s+/g, " ").trim();
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
  const want = norm(next.replace(/^error: /, "")).split(" ... ");
  const ok = errors.some((e) => { let at = 0; return want.every((w) => { const k = norm(e).indexOf(w, at); if (k < 0) return false; at = k + w.length; return true; }); });
  report(`examples.md: ${title}`, ok, `wanted "${want.join(" ... ")}", got ${JSON.stringify(errors.slice(0, 2))}`);
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
  report(`README: "${want.slice(0, 40)}..."`, got.some((e) => norm(e).startsWith(want.replace(/ on line \d+/, "")) || norm(e).includes(want.replace(/ on line \d+/, ""))), JSON.stringify(got.slice(0, 2)));
}
console.log(`docs: ${n - bad} of ${n} as the docs say`);
process.exit(bad ? 1 : 0);
