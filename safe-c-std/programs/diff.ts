// The ports print what suckless sbase's own programs print, on random input:
//   bun diff.ts DIR_OF_ORIGINALS DIR_OF_PORTS [rounds]
import { mkdtempSync, writeFileSync } from "fs";
const [orig, port, rounds = "300"] = process.argv.slice(2);
const d = mkdtempSync("/tmp/ports-");
let seed = 1;
const rnd = (n: number) => ((seed = (seed * 1103515245 + 12345) >>> 0) >>> 8) % n;
const pick = <T>(a: T[]) => a[rnd(a.length)];
const word = () => Array.from({ length: 1 + rnd(3) }, () => pick(["a", "b", "c", "1", "2", "-3", "10"])).join("");
const line = () => Array.from({ length: rnd(4) }, word).join(pick([" ", ":", "  ", "\t"]));
const lines = (n: number) => Array.from({ length: rnd(n) }, line);
const text = (l: string[]) => l.map(x => x + "\n").join("");
const flags = (o: string[]) => o.filter(() => rnd(3) == 0);
const run = (dir: string, t: string, args: string[], stdin: string) => {
  const r = Bun.spawnSync([`${dir}/${t}`, ...args], { stdin: Buffer.from(stdin), timeout: 10000 });
  return { out: r.stdout.toString(), code: r.exitCode, sig: r.signalCode };
};
const stats: Record<string, [number, number]> = {};
const check = (t: string, args: string[], stdin = "") => {
  const a = run(orig, t, args, stdin), b = run(port, t, args, stdin);
  const s = (stats[t] ??= [0, 0]);
  if (a.out === b.out && a.code === b.code) s[0]++;
  else if (s[1]++ < 2) console.log(t, JSON.stringify(args), JSON.stringify(stdin || text([])), "\n  sbase", JSON.stringify(a), "\n  port ", JSON.stringify(b));
};
for (let i = 0; i < +rounds; i++) {
  const a = `${d}/a`, b = `${d}/b`;
  writeFileSync(a, text(lines(12)));
  writeFileSync(b, text(lines(12)));
  check("wc", [...flags(["-c", "-l", "-w"]), ...pick([[a], [a, b], [a, "/nonexistent"]])]);
  check("wc", flags(["-l"]), text(lines(8)));
  check("uniq", [...flags(["-c"]), ...pick([[], ["-d"], ["-u"]]), ...pick([[], ["-f", "1"], ["-s", "2"]]), a]);
  check("sort", [...flags(["-r", "-u"]), ...pick([[], [a, b]])], text(lines(20)));
  check("sort", [...flags(["-r"]), "-n"], text(Array.from({ length: rnd(20) }, () => `${rnd(60) - 30}`)));
  const x = lines(12).filter(Boolean).sort(), y = lines(12).filter(Boolean).sort();
  const cmp = (p: string, q: string) => Buffer.compare(Buffer.from(p + "\n"), Buffer.from(q + "\n"));
  writeFileSync(a, text(x.sort(cmp)));
  writeFileSync(b, text(y.sort(cmp)));
  check("comm", [...flags(["-1", "-2", "-3"]), a, b]);
  const n = rnd(12) * 2;
  check("tsort", [], text(Array.from({ length: n / 2 }, () => `${pick("abcdefg".split(""))} ${pick("abcdefg".split(""))}`)));
}
let bad = 0;
for (const [t, [ok, no]] of Object.entries(stats)) (console.log(`${t}: ${ok} same, ${no} different`), (bad += no));
process.exit(bad ? 1 : 0);
