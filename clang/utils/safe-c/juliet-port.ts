// Ports NIST Juliet cases to Safe C into a separate tree, so their fixed
// good() versions can compile and be scored. Each transform is the change a
// person porting the code would make, and none removes a bug: see each rule.
//   bun juliet-port.ts <juliet root> <out root> [cwe,...]
import { join, dirname } from "path";
import { mkdirSync, cpSync, existsSync, readdirSync, statSync, readFileSync, writeFileSync } from "fs";

const [src, out, cweArg] = process.argv.slice(2);
// JULIET_PORT_OFF=f,g turns transforms off by letter, to gate each one alone.
const off = new Set((process.env.JULIET_PORT_OFF ?? "").split(",").filter(Boolean));
if (!src || !out) { console.error("usage: bun juliet-port.ts <juliet root> <out root> [121,122]"); process.exit(2); }
const cwes = (cweArg ?? "476,690,415,416,121,122,124,126,127,369").split(",");
const TC = join(src, "suite/C/testcases"), OTC = join(out, "suite/C/testcases");
cpSync(join(src, "suite/C/testcasesupport"), join(out, "suite/C/testcasesupport"), { recursive: true });
cpSync(join(import.meta.dir, "juliet_port.h"), join(out, "suite/C/testcasesupport/juliet_port.h"));

const stats: Record<string, number> = { files: 0, strfns: 0 };
// caseText: every file of the case as written (a, b, c... parts), comments out.
function port(text: string, file: string, caseText: string): string {
  text = text.replace(/\r\n/g, "\n"); // Juliet ships CRLF
  // A pointer local set to NULL needs no annotation: the compiler tracks which
  // of its values may be null, and a _Nullable declaration would forget that
  // a later assignment made it nonnull.
  // (f) Flow variants 05, 07 (and 09, 10, 13, 14 through juliet_port.h) pick
  // their path with a flag that is set once and never written: `static int
  // staticTrue = 1`. A porter declares such a flag const, which is what it
  // is, so the compiler sees which branch runs. Every path runs exactly as
  // before, so bad() still takes its buggy branch.
  if (!off.has("f")) text = text.replace(/^static int (static(?:True|False|Five)) = (\d+);/gm,
    (_m, name, v) => { stats.constFlags = (stats.constFlags ?? 0) + 1; return `static const int ${name} = ${v};`; });
  // (g) Flow variants 21 and 22 tell a sink which path to take through a
  // file-scope int (`badStatic = 1; badSink(data);`). Safe C rejects a
  // mutable global because threads could race on it; the program has one
  // thread, so a porter makes it _Thread_local, as the error suggests. Every
  // read sees the value the same thread just wrote, exactly as before.
  if (!off.has("g")) text = text.replace(/^(static |extern )?int (\w*(?:Static|Global))( = \d+)?;/gm,
    (_m, sc, name, init) => { stats.threadLocal = (stats.threadLocal ?? 0) + 1; return `${sc ?? ""}_Thread_local int ${name}${init ?? ""};`; });
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
    text = text.replace(/^(\s+)void \* (voidSecond|voidThird);/gm, (_m, i, n) => `${i}const ${/wchar_t charFirst/.test(text) ? "wchar_t" : "char"} *_Static ${n};`);
    text = text.replace(/(voidSecond\s*=\s*)\(void \*\)/g, "$1");
    text = text.replace(/\((?:char|wchar_t) \*\)(structCharVoid(?:\.|->)voidSecond)/g, "$1");
  }
  // (e) A buffer handed to a sink as `T * data` must carry its bounds
  // across the call (Rust: a slice); a plain pointer parameter is one
  // element, so the sink's copy would trap on correct code. The buggy
  // versions keep their buffers' real bounds and still trap.
  // Variants 63 and 64 pass a pointer to one variable (`dataPtr`), not a
  // buffer, and declare it as `data` in another file: leave them alone.
  const pointsToOne = /_6[34][a-z]?\.c$/.test(file);
  // ...but when that variable is the buffer pointer itself (`T * * dataPtr`),
  // the pointer it points to carries the buffer's bounds.
  if (pointsToOne)
    text = text.replace(/([(,]\s*)((?:const\s+)?[A-Za-z_]\w*) \* \* (data|dataPtr)(\s*[,)])/g,
      (_m, pre, ty, name, post) => { stats.slices = (stats.slices ?? 0) + 1; return `${pre}${ty} *__bidi_indexable * ${name}${post}`; });
  if (!pointsToOne) text = text.replace(/([(,]\s*)((?:const\s+)?[A-Za-z_]\w*) \* data(\s*[,)])/g,
    (_m, pre, ty, post) => { stats.slices = (stats.slices ?? 0) + 1; return `${pre}${ty} *__bidi_indexable data${post}`; });
  text = text.replace(/\(\*funcPtr\) \(((?:const\s+)?[A-Za-z_]\w*) \*\)/g, "(*funcPtr) ($1 *__bidi_indexable)");
  // ...and the same through a source function's result (variants 42, 61)
  // or a carrier's field (34, 67).
  text = text.replace(/^((?:static\s+)?(?:const\s+)?[A-Za-z_]\w*) \* ([A-Za-z_]\w*[Ss]ource\s*\()/gm,
    (_m, ty, fn) => { stats.slices = (stats.slices ?? 0) + 1; return `${ty} *__bidi_indexable ${fn}`; });
  text = text.replace(/^(\s+(?:const\s+)?[A-Za-z_]\w*) \* (structFirst|unionFirst|unionSecond);/gm, "$1 *__bidi_indexable $2;");
  // Outside the null and lifetime CWEs a null check cannot hide the bug: the
  // pointer is never null at run time, in bad() or good(), so it never fires.
  // If it did fire, exit(-1) scores as a miss. In CWE 476/690 null is the
  // bug, and in 415/416 a check could read a freed pointer: no checks there.
  const guardOk = /CWE(121|122|124|126|127|369)_/.test(file);
  const guard = (ind: string, v: string) => `\n${ind}if (${v} == NULL) {exit(-1);}`;
  const bump = (k: string) => { stats[k] = (stats[k] ?? 0) + 1; };
  const P = "\\*__bidi_indexable";
  // (h) A source handed a NULL variable to overwrite takes and may return
  // null, and the caller checks the result with Juliet's own idiom, as a Rust
  // port unwraps an Option.
  const nullSources = new Set([...caseText.matchAll(/\bdata = NULL;(?:[^;{}]*;)?\s*data = (\w*Source)\(data\);/g)].map(m => m[1]));
  if (!off.has("h")) {
    text = text.replace(new RegExp(`^((?:static\\s+)?(?:const\\s+)?\\w+ ${P}) (\\w*Source)\\(((?:const\\s+)?\\w+ ${P}) data\\)`, "gm"), (m, r, fn, p) => {
      if (!nullSources.has(fn)) return m;
      bump("nullableSources"); return `${r} _Nullable ${fn}(${p} _Nullable data)`; });
    if (guardOk) text = text.replace(/^([ \t]+)data = (\w*Source)\(data\);/gm, (m, ind, fn) => {
      if (!nullSources.has(fn)) return m;
      bump("sourceGuards"); return m + guard(ind, "data"); });
  }
  // (i) Variants 45 and 68 carry the data through a file-scope variable. Like
  // (g) it is _Thread_local. A heap buffer moved through one is _Owned, and
  // the sink that frees it takes it out. A stack buffer or interior pointer in
  // a global has no safe spelling (nor in Rust): it is _Unchecked, and its one
  // read is __unsafe (counted). The unsafe part is only its lifetime; the
  // pointer keeps its bounds, and the sink's accesses stay outside, checked.
  // In CWE 690 the global may be null and the sink decides.
  if (!off.has("i")) {
    const is690 = /CWE690_/.test(file);
    const movesHeap = /\bfree\(data\)/.test(caseText) && guardOk;
    text = text.replace(/^(static |extern )?((?:const )?(?:unsigned )?\w+) (\* )?(CWE\w+Data\w*);/gm, (m, sc, ty, ptr, name) => {
      if (ptr && !movesHeap && !(guardOk && /\*/.test(ptr)) && !is690) return m;
      bump("dataGlobals");
      return `${sc ?? ""}_Thread_local ${ty} ${ptr ? (movesHeap || is690 ? "*_Owned" : "*_Unchecked") + " __bidi_indexable _Nullable " : ""}${name};`;
    });
    if (movesHeap) text = text.replace(/^([ \t]+)((?:const )?\w+ \* data) = (CWE\w+Data\w*);/gm, (_m, ind, decl, g) => `${ind}${decl} = _Take(${g});${guard(ind, "data")}`);
    else if (is690) text = text.replace(/^([ \t]+\w+ \* data) = (CWE\w+Data\w*);/gm, "$1 = _Take($2);");
    else if (guardOk) text = text.replace(/^([ \t]+)((?:const )?\w+ \*) data = (CWE\w+Data\w*);/gm, (_m, ind, ty, g) => {
      bump("unsafeReads"); return `${ind}${ty} data = NULL;\n${ind}__unsafe { data = ${g}; }${guard(ind, "data")}`; });
  }
  // (j) A sink that frees its argument takes ownership of it (Rust: Box<T> by
  // value), and so does every function that passes its argument on to one.
  // (k) A sink that checks its argument for null before using it accepts
  // null (Rust: Option), and so does one that passes it on to such a sink.
  // A sink that does not check stays nonnull, so the null CWEs' bad paths are
  // still rejected.
  const fns = new Map<string, string>();
  for (const m of caseText.matchAll(/^[\w *]*?\b(\w+)\([^()]*\bdata\)\n\{\n([\s\S]*?)\n\}/gm)) fns.set(m[1], m[2]);
  const closure = (base: (b: string) => boolean) => {
    const set = new Set([...fns].filter(([, b]) => base(b)).map(([f]) => f));
    for (let grew = true; grew;) {
      grew = false;
      for (const [f, b] of fns) if (!set.has(f) && [...set].some(g => new RegExp(`\\b${g}\\(data\\)`).test(b))) { set.add(f); grew = true; }
    }
    return set;
  };
  const owns = closure(b => /\brealloc\(data,/.test(b) || (/\b(?:free|fclose)\(data\)/.test(b) && !/\bdata = (?!=)/.test(b)));
  const checks = closure(b => { const c = b.search(/if \(data [!=]= NULL\)/), w = b.search(/\bdata = (?!=)|\bdata\[|\*data\b|\(data[,)]/); return c >= 0 && (w < 0 || c < w); });
  text = text.replace(new RegExp(`\\b(\\w+)\\(((?:const )?\\w+ ${P}(?: _Nullable)?) data\\)`, "g"), (m, fn, p) => {
    const o = !off.has("j") && owns.has(fn), n = !off.has("k") && checks.has(fn) && !p.endsWith("_Nullable");
    if (o) bump("ownedParams");
    if (n) bump("nullableParams");
    return o || n ? `${fn}(${p}${n ? " _Nullable" : ""}${o ? " _Owned" : ""} data)` : m;
  });
  text = text.replace(new RegExp(`\\(\\*funcPtr\\) \\(((?:const )?\\w+ ${P})\\) = (\\w+);`, "g"), (_m, p, fn) =>
    `(*funcPtr) (${p}${checks.has(fn) && !off.has("k") ? " _Nullable" : ""})${owns.has(fn) && !off.has("j") ? " _Takes(1)" : ""} = ${fn};`);
  // (l) Variants 08, 11 and 17 set the data under a condition no compiler can
  // see through (a call's result, a loop's first iteration). Declare it
  // initialized; bad() then overwrites it with its buggy value as before. A
  // pointer starts null and is checked after the block that sets it.
  if (!off.has("l") && /_(08|11|17)\.c$/.test(file)) {
    text = text.replace(/^( {4})((?:const |unsigned )?\w+) (\* ?)?data;$/gm, (m, ind, ty, ptr) => {
      if (ptr && !guardOk) return m;
      bump("initDecls"); return `${ind}${ty} ${ptr ? "* data = NULL" : "data = 0"};`; });
    if (guardOk && /\* data = NULL;/.test(text))
      text = text.replace(/^ {4}(?:if|for) ?\([^\n]*\n {4}\{\n[\s\S]*?^ {4}\}(?:\n {4}else\n {4}\{\n[\s\S]*?^ {4}\})?/gm, m => {
        if (!/^ {8,12}data = /m.test(m)) return m;
        bump("initGuards"); return m + guard("    ", "data"); });
  }
  // (m) Variant 66 passes the data in an array of five: the sink's parameter
  // says how many elements it has, and the elements carry their own bounds.
  if (!off.has("m")) {
    text = text.replace(/\b((?:const )?\w+) \* dataArray\[(5?)\]/g, (_m, ty, n) => { bump("arrays"); return `${ty} *__bidi_indexable dataArray[${n || "__counted_by(5)"}]`; });
    text = text.replace(/\b((?:const )?(?:unsigned )?\w+) dataArray\[\]/g, (_m, ty) => { bump("arrays"); return `${ty} dataArray[__counted_by(5)]`; });
  }
  // (n) Variant 64 passes &data as void * and casts it straight back. Give
  // the parameter its real type and drop the cast.
  if (!off.has("n") && /_64[a-z]\.c$/.test(file)) {
    const t = caseText.match(/\(((?:const )?(?:unsigned )?\w+) \*( \*)?\)dataVoidPtr/);
    if (t) {
      const pt = t[2] ? `${t[1]} *__bidi_indexable *` : `${t[1]} *`;
      text = text.replace(/\bvoid \* dataVoidPtr/g, () => { bump("typedVoid"); return pt + " dataVoidPtr"; });
      text = text.replace(/^(\s+)[\w ]+\*(?: \*)? ?dataPtr = \([^()]*\)dataVoidPtr;/gm, `$1${pt} dataPtr = dataVoidPtr;`);
    }
  }
  // (o) Variant 32 reaches the data through two pointers to it: the pointee
  // carries the buffer's bounds. It may be null until the source sets it when
  // the case starts it at NULL.
  if (!off.has("o") && /_32\.c$/.test(file) && /\*dataPtr1 = &data;/.test(text)) {
    const nul = /\bdata = NULL;/.test(caseText) || /CWE690_/.test(file) ? " _Nullable" : "";
    text = text.replace(/^( {4})((?:const )?\w+) \* data;/gm, `$1$2 *__bidi_indexable${nul} data;`);
    text = text.replace(/^( {4})((?:const )?\w+) \* ?\*dataPtr([12]) = &data;/gm, (_m, i, ty, n) => { bump("ptrToPtr"); return `${i}${ty} *__bidi_indexable${nul} *dataPtr${n} = &data;`; });
    if (guardOk && nul) text = text.replace(/^( +)((?:const )?\w+ \* data = \*dataPtr2;)/gm, (m, ind) => m + guard(ind, "data"));
  }
  // (p) CWE 690's only good path checks the result in the sink, so whatever
  // carries it there may hold null: say so on the field, array element or
  // pointee. A bad sink that uses it unchecked is then rejected.
  if (!off.has("p") && /CWE690_/.test(file)) {
    text = text.replace(/\*__bidi_indexable (structFirst|unionFirst|unionSecond|dataArray\[)/g, (_m, n) => { bump("nullableCarriers"); return "*__bidi_indexable _Nullable " + n; });
    if (/_6[34][a-z]?\.c$/.test(file)) {
      text = text.replace(/\*__bidi_indexable \* (data\w*)/g, (_m, n) => { bump("nullableCarriers"); return "*__bidi_indexable _Nullable * " + n; });
      text = text.replace(/^( {4})(\w+) \* data;/gm, "$1$2 *__bidi_indexable _Nullable data;");
    }
  }
  // (q) CWE 476's char cases only read string literals through the pointer:
  // make it pointer to const, as the literal is.
  if (!off.has("q") && /CWE476_\w+__(char|wchar_t)_\d/.test(file)) {
    const ty = file.match(/__(char|wchar_t)_\d/)![1];
    text = text.replace(new RegExp(`(?<!const )\\b${ty} (\\*[^;)\\n]*\\bdata|\\*[^;)\\n]*(?:Data|structFirst|unionFirst|unionSecond|\\w*Source\\(|dataPtr|dataArray))`, "g"), (_m, r) => { bump("constText"); return `const ${ty} ${r}`; });
    text = text.replace(new RegExp(`(funcPtr\\) \\()(${ty} \\*)`, "g"), "$1const $2");
  }
  // (b) strcpy/strcat/wcscpy/wcscat: checked copies that trap (juliet_port.h).
  // The header also holds the constant flags of (f), so every file includes it.
  if (/\b(strcpy|strcat|wcscpy|wcscat|strncat|wcsncat|strncpy|wcsncpy)\s*\(/.test(text)) stats.strfns++;
  text = text.replace(/#include "std_testcase.h"\n/, '#include "std_testcase.h"\n' + (off.has("f") ? "#define JULIET_PORT_NO_CONST_FLAGS\n" : "") + '#include "juliet_port.h"\n');
  return text;
}
const caseCache = new Map<string, string>();
const stem = (p: string) => p.replace(/(_\d+)[a-z]?\.c$/, "$1");
function caseOf(p: string): string {
  const k = stem(p);
  let t = caseCache.get(k);
  if (t === undefined) {
    t = "abcde".split("").map(x => k + x + ".c").concat(k + ".c").filter(f => existsSync(f))
      .map(f => readFileSync(f, "utf8").replace(/\r\n/g, "\n").replace(/ *\/\*[\s\S]*?\*\//g, "")).join("\n");
    caseCache.set(k, t);
  }
  return t;
}
for (const d of readdirSync(TC)) {
  if (!cwes.some(c => d.startsWith(`CWE${c}_`))) continue;
  const walk = (dir: string) => {
    for (const e of readdirSync(dir)) {
      const p = join(dir, e), rel = p.slice(TC.length + 1), o = join(OTC, rel);
      if (statSync(p).isDirectory()) { walk(p); continue; }
      mkdirSync(dirname(o), { recursive: true });
      if (p.endsWith(".c")) { stats.files++; writeFileSync(o, port(readFileSync(p, "utf8"), p, caseOf(p))); }
      else cpSync(p, o);
    }
  };
  walk(join(TC, d));
}
console.log(JSON.stringify(stats));
