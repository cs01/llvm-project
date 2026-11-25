// The editor against a model: random keys, then save; the file is what the model has. bun edit.ts DIR_OF_PORTS [rounds]
import { writeFileSync, readFileSync, mkdtempSync } from "fs";
const [port, rounds = "400"] = process.argv.slice(2);
const d = mkdtempSync("/tmp/edit-"), f = `${d}/f`;
let seed = 11;
const rnd = (n: number) => ((seed = (Math.imul(seed, 1103515245) + 12345) >>> 0) >>> 8) % n;
let bad = 0, keys = 0, same = 0;
for (let r = 0; r < +rounds; r++) {
  let doc = Array.from({ length: rnd(200) }, () => "ab\n"[rnd(3)]).join("");
  writeFileSync(f, doc);
  let cur = 0, out = "", find = "";
  type C = { at: number; a: string; b: string };
  let done: C[] = [], undone: C[] = [];
  const start = (p: number) => doc.lastIndexOf("\n", p - 1) + 1 * 1 && p > 0 ? doc.lastIndexOf("\n", p - 1) + 1 : (p > 0 ? doc.lastIndexOf("\n", p - 1) + 1 : 0);
  const end = (p: number) => { const e = doc.indexOf("\n", p); return e < 0 ? doc.length : e; };
  const rep = (at: number, n: number, b: string) => { done.push({ at, a: doc.slice(at, at + n), b }); undone = []; doc = doc.slice(0, at) + b + doc.slice(at + n); cur = at + b.length; };
  const lines = (by: number) => { const s = start(cur), col = cur - s; let p = s; for (; by < 0 && p > 0; by++) p = start(p - 1); for (; by > 0 && end(p) < doc.length; by--) p = end(p) + 1; cur = Math.min(p + col, end(p)); };
  const next = () => { if (!find) return; for (let i = 1; i <= doc.length; i++) { const at = (cur + i) % (doc.length + 1); if (doc.startsWith(find, at)) { cur = at; return; } } };
  for (let n = rnd(120); n > 0; n--, keys++) {
    switch (rnd(17)) {
      case 0: out += "\x1b[A"; lines(-1); break;
      case 1: out += "\x1b[B"; lines(1); break;
      case 2: out += "\x1b[C"; cur = Math.min(cur + 1, doc.length); break;
      case 3: out += "\x1b[D"; cur = Math.max(cur - 1, 0); break;
      case 4: out += "\x1b[H"; cur = start(cur); break;
      case 5: out += "\x1b[F"; cur = end(cur); break;
      case 6: out += "\x1b[5~"; lines(-22); break;
      case 7: out += "\x1b[6~"; lines(22); break;
      case 8: out += "\x1b[3~"; if (cur < doc.length) rep(cur, 1, ""); break;
      case 9: out += "\x7f"; if (cur > 0) rep(cur - 1, 1, ""); break;
      case 10: case 11: { out += "\x1a"; const c = done.pop(); if (c) { undone.push(c); doc = doc.slice(0, c.at) + c.a + doc.slice(c.at + c.b.length); cur = c.at + c.a.length; } break; }
      case 12: { out += "\x19"; const c = undone.pop(); if (c) { done.push(c); doc = doc.slice(0, c.at) + c.b + doc.slice(c.at + c.a.length); cur = c.at + c.b.length; } break; }
      case 13: out += "\r"; rep(cur, 0, "\n"); break;
      case 14: { find = ["a", "ab", "zz", "b"][rnd(4)]; out += "\x06" + find + "x\x7f\r"; next(); break; }
      case 15: out += "\x07"; next(); break;
      default: { const c = "xyz \t"[rnd(5)]; out += c; rep(cur, 0, c); }
    }
  }
  const p = Bun.spawnSync([`${port}/edit`, f], { stdin: Buffer.from(out + "\x13\x11"), timeout: 20000 });
  const got = readFileSync(f, "utf8");
  if (p.exitCode === 0 && got === doc) same++;
  else if (bad++ < 3) console.log(r, p.exitCode, p.signalCode, JSON.stringify(out.slice(0, 200)), JSON.stringify(got.slice(0, 80)), JSON.stringify(doc.slice(0, 80)));
}
console.log(`edit: ${same} same, ${bad} different (${keys} keys)`);
process.exit(bad || keys < 1000 ? 1 : 0);
