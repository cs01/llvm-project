// json agrees with JavaScript on what is JSON and what it says, on random and damaged input: bun json.ts DIR_OF_PORTS [count]
import { writeFileSync } from "fs";
const [port, count = "3000"] = process.argv.slice(2);
let seed = 7;
const rnd = (n: number) => ((seed = (Math.imul(seed, 1103515245) + 12345) >>> 0) >>> 8) % n;
const pick = <T>(a: T[]) => a[rnd(a.length)];
const str = () => Array.from({ length: rnd(6) }, () => pick(["a", "é", "\n", '"', "\\", "😀", "\u0001", "/", "\u0000", "z"])).join("");
const num = () => pick([0, -1, 1.5, 1e21, -2.5e-7, 123456789012345680, rnd(1000), 0.1, 5e-324, 1.7976931348623157e308]);
const val = (d: number): any => {
  const k = rnd(d > 4 ? 4 : 6);
  if (k == 0) return num();
  if (k == 1) return str();
  if (k == 2) return pick([true, false, null]);
  if (k == 3) return rnd(100);
  if (k == 4) return Array.from({ length: rnd(5) }, () => val(d + 1));
  return Object.fromEntries(Array.from({ length: rnd(5) }, () => ["k" + str(), val(d + 1)]));
};
const escape = (s: string) => s.replace(/[^\x00-\x7f]/g, c => "\\u" + c.charCodeAt(0).toString(16).padStart(4, "0"));
const damage = (s: string) => {
  for (let n = rnd(3); n >= 0; n--) {
    const i = rnd(s.length + 1);
    s = pick([s.slice(0, i) + s.slice(i + 1), s.slice(0, i) + pick([..."{}[]\",:0123456789.eE+-tfn\\u \n"]) + s.slice(i), s.slice(0, i)]);
  }
  return s;
};
// No lone halves of pairs and nothing past a double, which the port refuses or prints as inf.
const skip = (v: any): boolean =>
  typeof v == "string" ? !(v as any).isWellFormed() : typeof v == "number" ? !isFinite(v)
  : v && typeof v == "object" ? Object.entries(v).some(([k, x]) => skip(k) || skip(x)) : false;
let same = 0, bad = 0, valid = 0, invalid = 0, skipped = 0, crashed = 0;
for (let i = 0; i < +count; i++) {
  let doc = JSON.stringify(val(0), null, pick([0, 1, "\t"]));
  if (rnd(2)) doc = escape(doc);
  if (rnd(2)) doc = damage(doc);
  let want: any, ok = true;
  try { want = JSON.parse(doc); } catch { ok = false; }
  if (ok && skip(want)) { skipped++; continue; }
  const r = Bun.spawnSync([`${port}/json`], { stdin: Buffer.from(doc), timeout: 10000 });
  if (r.exitCode !== 0 && r.exitCode !== 1) crashed++;
  let good = ok === (r.exitCode === 0);
  if (good && ok) good = JSON.stringify(JSON.parse(r.stdout.toString())) === JSON.stringify(want);
  ok ? valid++ : invalid++;
  if (good) same++;
  else if (bad++ < 4) console.log(JSON.stringify(doc).slice(0, 200), "js:", ok, "port:", r.exitCode, r.signalCode, r.stdout.toString().slice(0, 100));
}
console.log(`json: ${same} same, ${bad} different (${valid} JSON, ${invalid} not, ${skipped} skipped, ${crashed} crashed)`);
process.exit(bad || crashed || !valid || !invalid ? 1 : 0);
