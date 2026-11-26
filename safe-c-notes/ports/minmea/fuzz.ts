// The port against the original on GPS sentences, real ones and broken ones:
//   bun fuzz.ts ORIGINAL_HARNESS PORT_HARNESS [count]
const [orig, port, count = "20000"] = process.argv.slice(2);
let seed = 5;
const rnd = (n: number) => ((seed = (Math.imul(seed, 1103515245) + 12345) >>> 0) >>> 8) % n;
const pick = <T>(a: T[]) => a[rnd(a.length)];
const digits = (n: number) => Array.from({ length: n }, () => "0123456789"[rnd(10)]).join("");
const good = () => pick([digits(1 + rnd(3)), digits(1 + rnd(4)) + "." + digits(1 + rnd(4)), "-" + digits(1 + rnd(2)) + "." + digits(2), ""]);
const num = () => rnd(40) < 39 ? good() : pick([digits(1 + rnd(3)), digits(1 + rnd(4)) + "." + digits(rnd(6)), "-" + digits(2), "+" + digits(2), "", " " + digits(2),
  digits(12), "0." + "0".repeat(rnd(40)) + "1", digits(9) + "." + digits(9), "1e5", ".5", "-", "+-1", "99999999999999999999", "2147483648", "-2147483649"]);
const time = () => rnd(10) < 8 ? pick([digits(6), digits(6) + "." + digits(1 + rnd(3))]) : pick([digits(6) + "." + digits(rnd(9)), digits(4), "", "12345a", digits(6) + "."]);
const date = () => rnd(10) < 8 ? digits(6) : pick(["", digits(5), "12a456"]);
const dir = (a: string) => rnd(10) < 8 ? a : pick(["", "X", a.toLowerCase()]);
const c = (s: string) => rnd(10) < 8 ? s : pick(["", "?"]);
const talker = () => rnd(10) < 9 ? pick(["GP", "GN", "GL", "BD"]) : pick(["G", "GPX"]);
const bodies = [
  () => `RMC,${time()},${c("A")},${num()},${dir("S")},${num()},${dir("E")},${num()},${num()},${date()},${num()},${dir("W")}`,
  () => `GGA,${time()},${num()},${dir("N")},${num()},${dir("W")},${num()},${num()},${num()},${num()},${c("M")},${num()},${c("M")},${num()},${num()}`,
  () => `GSA,${c("A")},${num()},` + Array.from({ length: 12 }, num).join(",") + `,${num()},${num()},${num()}`,
  () => `GLL,${num()},${dir("N")},${num()},${dir("W")},${time()},${c("A")}` + pick(["", `,${c("A")}`]),
  () => `GST,${time()},` + Array.from({ length: 7 }, num).join(","),
  () => `GSV,${num()},${num()},${num()}` + Array.from({ length: rnd(5) }, () => `,${num()},${num()},${num()},${num()}`).join(""),
  () => `VTG,${num()},${c("T")},${num()},${c("M")},${num()},${c("N")},${num()},${c("K")}` + pick(["", `,${c("A")}`]),
  () => `ZDA,${time()},${num()},${num()},${num()},${num()},${num()}`,
  () => `GBS,${time()},${num()},${num()},${num()},${num()},${num()},${num()},${num()}`,
];
const checksum = (s: string) => { let x = 0; for (const ch of s) x ^= ch.charCodeAt(0); return x.toString(16).toUpperCase().padStart(2, "0"); };
const damage = (s: string) => {
  for (let n = rnd(3); n > 0; n--) {
    const i = rnd(s.length + 1);
    s = pick([s.slice(0, i) + s.slice(i + 1), s.slice(0, i) + pick([..."$,*.-+ 0A\t~"]) + s.slice(i), s.slice(0, i), s.slice(0, i) + "," + s.slice(i)]);
  }
  return s;
};
const lines: string[] = [];
for (let i = 0; i < +count; i++) {
  const body = talker() + pick(bodies)();
  let line = "$" + body + pick([`*${checksum(body)}`, `*${checksum(body).toLowerCase()}`, "", "*00", "*"]) + pick(["", "", "\\r", "\\r\\r"]);
  if (rnd(3) == 0) line = damage(line);
  lines.push(line.replace(/\n/g, ""));
}
const input = Buffer.from(lines.join("\n") + "\n");
if (process.env.LINES_OUT) await Bun.write(process.env.LINES_OUT, input);
const run = (bin: string) => { const r = Bun.spawnSync([bin], { stdin: input, timeout: 120000 }); return { out: r.stdout.toString(), code: r.exitCode, err: r.stderr.toString() }; };
const a = run(orig), b = run(port);
if (a.code !== 0 || b.code !== 0) console.log("exit", a.code, b.code, a.err.slice(0, 500), b.err.slice(0, 500));
const sa = a.out.split("\n"), sb = b.out.split("\n");
// The original overflows a number's scale past nine decimals (undefined
// behaviour); the port drops those digits, so those lines may differ.
const overflows = (l: string) => /\.[0-9]{10,}/.test(l);
let same = 0, diff = 0, parsed = 0, expected = 0;
// One block per input line, each starting with "chk".
const blocks = (x: string[]) => { const out: string[][] = []; for (const l of x) { if (l.startsWith("chk")) out.push([l]); else if (l && out.length) out[out.length - 1].push(l); } return out; };
const ba = blocks(sa), bb = blocks(sb);
for (let i = 0; i < Math.max(ba.length, bb.length); i++) {
  const x = (ba[i] ?? []).join("\n"), y = (bb[i] ?? []).join("\n");
  parsed += (ba[i]?.length ?? 1) - 1;
  if (x === y) same++;
  else if (overflows(lines[i])) expected++;
  else if (diff++ < (process.env.ALL ? 1e9 : 4)) console.log("line:", JSON.stringify(lines[i]), "\n  original:", x.replace(/\n/g, " | "), "\n  port:    ", y.replace(/\n/g, " | "));
}
console.log(`minmea: ${same} same, ${diff} different, ${expected} different where the original overflows, of ${lines.length} lines (${parsed} parsed by some parser)`);
process.exit(diff || ba.length !== lines.length || parsed < lines.length / 4 ? 1 : 0);
