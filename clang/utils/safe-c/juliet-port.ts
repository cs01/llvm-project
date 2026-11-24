// Ports NIST Juliet cases to Safe C into a separate tree, so their fixed
// good() versions can compile and be scored. Each transform is the change a
// person porting the code would make, and none removes a bug: see each rule.
//   bun juliet-port.ts <juliet root> <out root> [cwe,...]
import { join, dirname } from "path";
import { mkdirSync, cpSync, readdirSync, statSync, readFileSync, writeFileSync } from "fs";

const [src, out, cweArg] = process.argv.slice(2);
if (!src || !out) { console.error("usage: bun juliet-port.ts <juliet root> <out root> [121,122]"); process.exit(2); }
const cwes = (cweArg ?? "476,690,415,416,121,122,124,126,127,369").split(",");
const TC = join(src, "suite/C/testcases"), OTC = join(out, "suite/C/testcases");
cpSync(join(src, "suite/C/testcasesupport"), join(out, "suite/C/testcasesupport"), { recursive: true });
cpSync(join(import.meta.dir, "juliet_port.h"), join(out, "suite/C/testcasesupport/juliet_port.h"));

const stats: Record<string, number> = { files: 0, nullable: 0, strfns: 0 };
function port(text: string): string {
  text = text.replace(/\r\n/g, "\n"); // Juliet ships CRLF
  // (a) A pointer local set to NULL may be null: say so. (Rust: Option<&T>.)
  text = text.replace(/^(\s+)((?:const\s+)?[A-Za-z_][\w ]*?\s*\*+)\s*([A-Za-z_]\w*)\s*=\s*NULL\s*;/gm,
    (_m, ind, ty, name) => { stats.nullable++; return `${ind}${ty.trimEnd()} _Nullable ${name} = NULL;`; });
  // ...and a pointer local declared bare and set to NULL later.
  const nulled = new Set([...text.matchAll(/\b([A-Za-z_]\w*)\s*=\s*NULL\s*;/g)].map(m => m[1]));
  text = text.replace(/^(\s+)((?:const\s+)?[A-Za-z_][\w ]*?\s*\*+)\s*([A-Za-z_]\w*)\s*;/gm,
    (m, ind, ty, name) => {
      if (!nulled.has(name) || /\b(return|goto|case)\b/.test(ty)) return m;
      stats.nullable++; return `${ind}${ty.trimEnd()} _Nullable ${name};`;
    });
  // (c) Flow variants 34 and 67 pass the data through a union or struct
  // that lives only in locals and parameters: a borrow carrier, which Safe
  // C spells _View (Rust: a struct with a lifetime).
  text = text.replace(/typedef union\s*\n\{/g, () => { stats.views = (stats.views ?? 0) + 1; return "typedef union _View\n{"; });
  text = text.replace(/typedef struct (\w*_structType)\b/g, (_m, name) => { stats.views = (stats.views ?? 0) + 1; return `typedef struct _View ${name}`; });
  // (d) charVoid keeps a string literal in void * fields and casts it back
  // to print it: give the fields their real type (static text). The bug,
  // writing past charFirst into those fields, is unchanged.
  if (text.includes("voidSecond")) {
    stats.statics = (stats.statics ?? 0) + 1;
    text = text.replace(/^(\s+)void \* (voidSecond|voidThird);/gm, "$1const char *_Static $2;");
    text = text.replace(/(voidSecond\s*=\s*)\(void \*\)/g, "$1");
    text = text.replace(/\(char \*\)(structCharVoid(?:\.|->)voidSecond)/g, "$1");
  }
  // (b) strcpy/strcat/wcscpy/wcscat: checked copies that trap (juliet_port.h).
  if (/\b(strcpy|strcat|wcscpy|wcscat|strncat|wcsncat)\s*\(/.test(text)) {
    stats.strfns++;
    text = text.replace(/#include "std_testcase.h"\n/, '#include "std_testcase.h"\n#include "juliet_port.h"\n');
  }
  return text;
}
for (const d of readdirSync(TC)) {
  if (!cwes.some(c => d.startsWith(`CWE${c}_`))) continue;
  const walk = (dir: string) => {
    for (const e of readdirSync(dir)) {
      const p = join(dir, e), rel = p.slice(TC.length + 1), o = join(OTC, rel);
      if (statSync(p).isDirectory()) { walk(p); continue; }
      mkdirSync(dirname(o), { recursive: true });
      if (p.endsWith(".c")) { stats.files++; writeFileSync(o, port(readFileSync(p, "utf8"))); }
      else cpSync(p, o);
    }
  };
  walk(join(TC, d));
}
console.log(JSON.stringify(stats));
