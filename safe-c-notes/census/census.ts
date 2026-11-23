// Safe C census: compile real C projects with -fsafe-c and count errors by category.
// Run: bun census.ts [project ...]   (no args = all)
import { Glob } from "bun";
import { readFileSync, writeFileSync } from "fs";
import { join } from "path";

const ROOT = import.meta.dir;
const SRC = join(ROOT, "src");
const BUILD = join(ROOT, "build");
const CC = process.env.HOME + "/git/llvm-safe-c/build.noindex/bin/clang";
const SDK = (await Bun.$`xcrun --show-sdk-path`.text()).trim();

type Proj = {
  name: string;
  dir: string;
  files: string[]; // globs relative to dir
  exclude?: string[];
  inc?: string[]; // relative to dir, or absolute
  defs?: string[];
  headers?: string[]; // globs counted for LOC
  prelude?: string; // generated one-line TU contents (stb)
  notes?: string;
};

const P: Proj[] = [
  { name: "inih", dir: "inih", files: ["ini.c"], headers: ["ini.h"] },
  { name: "parson", dir: "parson", files: ["parson.c"], headers: ["parson.h"] },
  { name: "sds", dir: "sds", files: ["sds.c"], headers: ["*.h"], inc: ["."] },
  { name: "lz4", dir: "lz4", files: ["lib/*.c"], headers: ["lib/*.h"], inc: ["lib"] },
  { name: "stb_image", dir: "stb", files: [], headers: ["stb_image.h"], inc: ["."],
    prelude: '#define STB_IMAGE_IMPLEMENTATION\n#include "stb_image.h"\n', notes: "one-line TU" },
  { name: "zlib", dir: "zlib", files: ["*.c"], headers: ["*.h"], inc: ["."], defs: ["Z_HAVE_UNISTD_H"] },
  { name: "expat", dir: "libexpat/expat", files: ["lib/xmlparse.c", "lib/xmlrole.c", "lib/xmltok.c"],
    headers: ["lib/*.h"], inc: ["lib", BUILD + "/expat"], defs: ["HAVE_EXPAT_CONFIG_H"],
    notes: "expat_config.h from cmake configure into build/expat; xmltok_impl/ns are #included so not standalone TUs" },
  { name: "cmark", dir: "cmark", files: ["src/*.c"], exclude: ["src/main.c"], headers: ["src/*.h"],
    inc: ["src", BUILD + "/cmark/src"], notes: "cmake configure into build/cmark" },
  { name: "libyaml", dir: "libyaml", files: ["src/*.c"], headers: ["src/*.h", "include/*.h"],
    inc: ["include", "src"],
    defs: ["YAML_VERSION_MAJOR=0", "YAML_VERSION_MINOR=2", "YAML_VERSION_PATCH=5", 'YAML_VERSION_STRING="0.2.5"'],
    notes: "YAML_VERSION_* via -D" },
  { name: "lua", dir: "lua", files: ["*.c"], exclude: ["lua.c", "luac.c", "onelua.c", "ltests.c"], headers: ["*.h"], inc: ["."] },
  { name: "llhttp", dir: "llhttp", files: ["src/*.c"], headers: ["include/*.h"], inc: ["include"], notes: "release branch (prebuilt C)" },
  { name: "sqlite", dir: "sqlite", files: ["sqlite3.c"], headers: ["sqlite3.h"], inc: ["."],
    notes: "amalgamation 3.54.1" },
  { name: "redis", dir: "redis", files: ["sds", "dict", "adlist", "quicklist", "listpack", "ziplist", "zmalloc"].map(f => `src/${f}.c`),
    headers: ["src/sds.h", "src/dict.h", "src/adlist.h", "src/quicklist.h", "src/listpack.h", "src/ziplist.h", "src/zmalloc.h"],
    inc: ["src", "deps/hdr_histogram", "deps/jemalloc/include"], defs: ["REDIS_STATIC="], notes: "7 files only" },
  { name: "zstd", dir: "zstd", files: ["lib/common/*.c", "lib/compress/*.c", "lib/decompress/*.c", "lib/dictBuilder/*.c"],
    headers: ["lib/*.h", "lib/**/*.h"], inc: ["lib", "lib/common"], notes: "library only (no legacy/deprecated)" },
  { name: "openzl", dir: "openzl", files: ["src/openzl/**/*.c"], headers: ["include/**/*.h", "src/**/*.h"],
    inc: [".", "include", "src"], notes: "core library (src/openzl), flags from compile_flags.txt" },
  { name: "bearssl", dir: "BearSSL", files: ["src/**/*.c"], headers: ["inc/*.h", "src/**/*.h"], inc: ["inc", "src"] },
];

const CATS: [string, RegExp][] = [
  ["ownership", /frees its parameter|ownership_takes|does not live long enough|lifetimebound|stack memory associated/],
  ["null", /nullable|nonnull|null pointer/],
  ["terminated-string", /terminated_by|__null_terminated|terminator|linear search/],
  ["bounds-single", /single pointer|__single|constant index of 0|pointer arithmetic on/],
  ["bounds-other", /counted_by|sized_by|bounds|__bidi_indexable|__indexable|out of bounds/],
  ["unchecked-libc", /unavailable|drops its bounds|__unsafe_indexable/],
  ["casts-types", /wrong type|makes up an address|can forge a pointer|union|incompatible/],
  ["init", /uninitialized|initialize/],
  ["format-varargs", /format|prototype|scanf/],
  ["escape", /can switch off Safe C|__unsafe/],
  ["other", /./],
];
const classify = (m: string) => CATS.find(([, r]) => r.test(m))![0];
const norm = (m: string) => m.replace(/'[^']*'/g, "'X'").replace(/"[^"]*"/g, '"X"');

function expand(dir: string, globs: string[], exclude: string[] = []) {
  const out = new Set<string>();
  for (const g of globs) for (const f of new Glob(g).scanSync({ cwd: dir })) out.add(f);
  for (const e of exclude) out.delete(e);
  return [...out].sort();
}
const loc = (f: string) => readFileSync(f, "utf8").split("\n").filter(l => l.trim()).length;

async function run(args: string[], cwd: string) {
  const p = Bun.spawn(args, { cwd, stdout: "pipe", stderr: "pipe", timeout: 300000 });
  const [o, e] = await Promise.all([new Response(p.stdout).text(), new Response(p.stderr).text()]);
  await p.exited;
  return { out: o + e, code: p.exitCode, signal: p.signalCode };
}
const ERR = /^(.+?):(\d+):(\d+): (?:fatal )?error: (.*)$/;

async function pool<T>(items: T[], n: number, fn: (x: T) => Promise<void>) {
  let i = 0;
  await Promise.all(Array.from({ length: n }, async () => { while (i < items.length) await fn(items[i++]); }));
}

async function census(p: Proj) {
  const dir = join(SRC, p.dir);
  const files = expand(dir, p.files, p.exclude);
  const tus: string[] = [...files];
  if (p.prelude) {
    const f = join(BUILD, p.name + "_tu.c");
    writeFileSync(f, p.prelude);
    tus.push(f);
  }
  const common = ["-fsyntax-only", "-ferror-limit=0", "-isysroot", SDK,
    ...(p.inc ?? []).map(i => "-I" + (i.startsWith("/") ? i : join(dir, i))),
    ...(p.defs ?? []).map(d => "-D" + d)];
  const hdrs = expand(dir, p.headers ?? []);
  const LOC = [...files.map(f => join(dir, f)), ...hdrs.map(f => join(dir, f))].reduce((s, f) => s + loc(f), 0);
  const res: any = { name: p.name, loc: LOC, tus: 0, notes: p.notes ?? "", skippedTUs: [] as any[], crashes: [] as any[] };
  const seen = new Set<string>();
  const errs: { cat: string; msg: string; loc: string }[] = [];
  let sysErrs = 0;
  const sysMsgs = new Map<string, number>();
  const plainFail: string[] = [];
  let okTUs = 0;
  await pool(tus, 8, async tu => {
    const abs = tu.startsWith("/") ? tu : join(dir, tu);
    const base = [CC, ...common, abs];
    const plain = await run(base, dir);
    const plainErrs = plain.out.split("\n").filter(l => ERR.test(l));
    if (plainErrs.length || plain.code !== 0) { plainFail.push(`${tu}: ${plainErrs[0] ?? "exit " + plain.code}`); return; }
    okTUs++;
    const cmd = [CC, "-fsafe-c", ...common, abs];
    const r = await run(cmd, dir);
    if (/PLEASE submit a bug report|Stack dump/.test(r.out) || r.signal)
      res.crashes.push({ file: tu, command: cmd.join(" ") });
    for (const l of r.out.split("\n")) {
      const m = ERR.exec(l);
      if (!m) continue;
      const [, f, ln, col, msg] = m;
      const full = f.startsWith("/") ? f : join(dir, f);
      const inProj = full.startsWith(dir + "/") || full.startsWith(BUILD + "/");
      const key = `${full}:${ln}:${col}:${msg}`;
      if (seen.has(key)) continue;
      seen.add(key);
      if (!inProj) { sysErrs++; const k = norm(msg); sysMsgs.set(k, (sysMsgs.get(k) ?? 0) + 1); continue; }
      const rel = full.startsWith(dir + "/") ? full.slice(dir.length + 1) : full;
      errs.push({ cat: classify(msg), msg, loc: `${rel}:${ln}:${col}` });
    }
  });
  res.tus = okTUs;
  res.skippedTUs = plainFail;
  res.setupFailed = okTUs === 0 ? (plainFail[0] ?? "no TUs") : null;
  res.total = errs.length;
  res.perKLOC = LOC ? +(errs.length / (LOC / 1000)).toFixed(2) : 0;
  res.systemHeaderErrors = sysErrs;
  res.systemTop = [...sysMsgs].sort((a, b) => b[1] - a[1]).slice(0, 3);
  res.cats = {};
  for (const [c] of CATS) {
    const es = errs.filter(e => e.cat === c);
    const cnt = new Map<string, { n: number; ex: string; raw: string }>();
    for (const e of es) {
      const k = norm(e.msg);
      const v = cnt.get(k) ?? { n: 0, ex: e.loc, raw: e.msg };
      v.n++; cnt.set(k, v);
    }
    res.cats[c] = {
      count: es.length,
      top: [...cnt].sort((a, b) => b[1].n - a[1].n).slice(0, c === "other" ? 5 : 3)
        .map(([m, v]) => ({ msg: c === "other" ? v.raw : m, n: v.n, example: v.ex })),
    };
  }
  return res;
}

const want = process.argv.slice(2);
const results: any[] = [];
for (const p of P) {
  if (want.length && !want.includes(p.name)) continue;
  console.error("==", p.name);
  const r = await census(p);
  console.error(`   ${r.setupFailed ? "SETUP FAILED " + r.setupFailed : r.total + " errors / " + r.tus + " TUs"}`);
  results.push(r);
}
const prev = (() => { try { return JSON.parse(readFileSync(join(ROOT, "results.json"), "utf8")); } catch { return []; } })();
const merged = want.length ? [...prev.filter((x: any) => !want.includes(x.name)), ...results] : results;
writeFileSync(join(ROOT, "results.json"), JSON.stringify(merged, null, 1).replaceAll(process.env.HOME!, "~"));

// RESULTS.md
const ok = merged.filter((r: any) => !r.setupFailed).sort((a: any, b: any) => b.perKLOC - a.perKLOC);
const bad = merged.filter((r: any) => r.setupFailed);
const names = CATS.map(c => c[0]);
let md = "# Safe C census\n\n| project | LOC | TUs | total | err/kLOC | " + names.join(" | ") + " | notes |\n|" + "---|".repeat(names.length + 6) + "\n";
for (const r of ok) {
  const skip = r.skippedTUs.length ? ` (${r.skippedTUs.length} TUs failed plain C, skipped)` : "";
  md += `| ${r.name} | ${r.loc} | ${r.tus} | ${r.total} | ${r.perKLOC} | ${names.map(n => r.cats[n].count).join(" | ")} | ${r.notes}${skip}${r.crashes.length ? " CRASHES:" + r.crashes.length : ""} |\n`;
}
for (const r of bad) md += `| ${r.name} | ${r.loc} | 0 | setup failed | | | | | | | | | | | ${r.setupFailed.replace(/\|/g, "/")} |\n`;
md += "\n## Totals by category\n\n";
const tot = names.map(n => [n, merged.reduce((s: number, r: any) => s + (r.cats?.[n]?.count ?? 0), 0)] as [string, number]).sort((a, b) => b[1] - a[1]);
md += tot.map(([n, c]) => `- ${n}: ${c}`).join("\n") + "\n";
md += "\n## Messages by category (all projects, top per project)\n";
for (const n of names) {
  md += `\n### ${n}\n`;
  for (const r of ok) {
    const c = r.cats[n];
    if (!c.count) continue;
    md += `- **${r.name}** (${c.count})\n`;
    for (const t of c.top) md += `  - ${t.n}x \`${t.msg}\` e.g. ${t.example}\n`;
  }
}
md += "\n## System header errors\n";
for (const r of merged) if (r.systemHeaderErrors) md += `- ${r.name}: ${r.systemHeaderErrors} ${JSON.stringify(r.systemTop)}\n`;
md += "\n## Compiler crashes\n";
const crashes = merged.flatMap((r: any) => r.crashes.map((c: any) => ({ p: r.name, ...c })));
md += crashes.length ? crashes.map((c: any) => `- ${c.p} ${c.file}: \`${c.command}\``).join("\n") : "none";
md += "\n\n## Skipped TUs (failed plain C)\n";
for (const r of merged) for (const s of r.skippedTUs) md += `- ${r.name}: ${s}\n`;
writeFileSync(join(ROOT, "RESULTS.md"), (md + "\n").replaceAll(process.env.HOME!, "~"));
