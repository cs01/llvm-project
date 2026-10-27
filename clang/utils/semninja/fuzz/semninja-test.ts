// End-to-end check of semninja on a small ninja project (the fuzz TUs and a
// PCH, wired the way CMake does it): after a header edit it recompiles only
// what the edit can affect, the objects it keeps are byte-identical to a
// fresh compile, and plain ninja afterwards has nothing left to do.
//
// usage: bun semninja-test.ts <bin dir with clang++ and clang-semindex>
import { execFileSync, spawnSync } from "child_process";
import { cpSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from "fs";
import { join } from "path";

const bin = process.argv[2];
const here = import.meta.dir;
const work = join(process.env.TMPDIR ?? "/tmp", "semninja-test");
rmSync(work, { recursive: true, force: true });
mkdirSync(work, { recursive: true });
const sdk = execFileSync("xcrun", ["--show-sdk-path"]).toString().trim();
const tus = readdirSync(here).filter((f) => /^tu_.*\.cpp$/.test(f));
for (const f of [...tus, "lib.h"]) cpSync(join(here, f), join(work, f));
const base = readFileSync(join(work, "lib.h"), "utf8");

const flags = `-isysroot ${sdk} -std=c++17 -O2 -fno-exceptions`;
writeFileSync(join(work, "build.ninja"), `
rule pch
  command = ${bin}/clang++ ${flags} -x c++-header $in -o $out -MD -MF $out.d
  depfile = $out.d
  deps = gcc
rule cxx
  command = ${bin}/clang++ ${flags} -Xclang -include-pch -Xclang lib.pch -Xclang -include -Xclang lib.h -Xclang -fsemantic-deps -MD -MF $out.d -c $in -o $out
  depfile = $out.d
  deps = gcc
build lib.pch: pch lib.h
${tus.map((t) => `build ${t}.o: cxx ${t} | lib.pch`).join("\n")}
build all: phony ${tus.map((t) => `${t}.o`).join(" ")}
default all
`);

const sh = (cmd: string[], env = {}) => {
  const r = spawnSync(cmd[0], cmd.slice(1), { cwd: work, encoding: "utf8", env: { ...process.env, ...env } });
  if (r.status !== 0) throw new Error(`${cmd.join(" ")}\n${r.stdout}\n${r.stderr}`);
  return r.stdout + r.stderr;
};
const ninja = () => sh(["ninja", "-C", work]);
const semninja = (...args: string[]) =>
  sh(["bun", join(here, "..", "semninja.ts"), "-C", work, "--indexer", `${bin}/clang-semindex`, ...args]);
// Ninja prints one "[n/m] ..." line per edge it runs; count the TU compiles.
const tuRuns = (out: string) => out.split("\n").filter((l) => /^\[\d+\/\d+\] .* -c tu_/.test(l)).length;

let failures = 0;
const expect = (what: string, ok: boolean) => {
  console.log(`${ok ? "ok  " : "FAIL"} ${what}`);
  if (!ok) failures++;
};

ninja();
writeFileSync(join(work, "compile_commands.json"), sh(["ninja", "-C", work, "-t", "compdb", "cxx"]).replace(/^ninja: .*\n/m, ""));
semninja(); // first run: records the headers' contents

const cases: [string, string, string, number][] = [
  ["an edit nothing uses", "int sum() const", "int extra() const { return 9; }\n  int sum() const", 0],
  ["an edit only tu_point uses", "return x + y;", "return y + x + 0 * x;", 1],
];
let header = base;
for (const [what, from, to, expected] of cases) {
  header = header.replace(from, to);
  writeFileSync(join(work, "lib.h"), header);
  const out = semninja();
  expect(`${what}: ${expected} TU(s) recompiled (got ${tuRuns(out)})`, tuRuns(out) === expected);
  expect(`${what}: kept objects identical to a fresh compile`, /verify: (\d+)\/\1 kept/.test(semninja("--verify-last")));
  expect(`${what}: plain ninja has nothing left to do`, /no work to do/.test(ninja()));
}
console.log(failures ? `\n${failures} FAILED` : "\nall passed");
process.exit(failures ? 1 : 0);
