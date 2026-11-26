// The port against the original: compress random data, decompress it, and
// decompress damaged and random bytes (what a device may be sent).
//   bun fuzz.ts ORIGINAL PORT [rounds]
const [orig, port, rounds = "400"] = process.argv.slice(2);
let seed = 9;
const rnd = (n: number) => ((seed = (Math.imul(seed, 1103515245) + 12345) >>> 0) >>> 8) % n;
const data = () => {
  const n = rnd(6) == 0 ? rnd(40000) : rnd(3000), b = Buffer.alloc(n), kind = rnd(4);
  for (let i = 0; i < n; i++) b[i] = kind == 0 ? rnd(256) : kind == 1 ? "abcab "[rnd(6)].charCodeAt(0) : kind == 2 ? (i % 7) * 3 : (i > 8 && rnd(3) ? b[i - 1 - rnd(8)] : rnd(256));
  return b;
};
const run = (bin: string, mode: string, s: number, input: Buffer) => {
  const r = Bun.spawnSync([bin, mode, String(s)], { stdin: input, timeout: 30000 });
  return { out: r.stdout, code: r.exitCode, sig: r.signalCode };
};
let same = 0, diff = 0, roundTrips = 0, garbage = 0;
const check = (what: string, mode: string, input: Buffer) => {
  const s = rnd(1000000);
  const a = run(orig, mode, s, input), b = run(port, mode, s, input);
  if (a.code === b.code && a.sig === b.sig && Buffer.compare(a.out, b.out) === 0) same++;
  else if (diff++ < 4) console.log(what, mode, input.length, "original", a.code, a.sig, a.out.length, "port", b.code, b.sig, b.out.length);
  return b;
};
for (let i = 0; i < +rounds; i++) {
  const d = data();
  const c = check("data", "c", d);
  const back = check("compressed", "d", c.out);
  if (back.code === 0 && Buffer.compare(back.out, d) === 0) roundTrips++;
  const bad = Buffer.from(c.out);
  for (let k = rnd(4) + 1; k > 0 && bad.length; k--) bad[rnd(bad.length)] ^= 1 << rnd(8);
  check("damaged", "d", bad);
  const junk = Buffer.alloc(rnd(5000)); for (let k = 0; k < junk.length; k++) junk[k] = rnd(256);
  check("random", "d", junk);
  garbage += 2;
}
console.log(`heatshrink: ${same} same, ${diff} different; ${roundTrips} of ${rounds} round trips give the data back; ${garbage} damaged or random inputs decoded`);
process.exit(diff || roundTrips < +rounds ? 1 : 0);
