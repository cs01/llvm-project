// Splits a 64-bit Mach-O relocatable object into atoms (the byte ranges
// between symbols, the same units ld64 dead-strips and orders) and gives each
// one a hash of its bytes and relocations. A relocation to a named symbol
// hashes the name, so a function's hash does not change when only its callee
// changes; a relocation to an anonymous atom (a string literal, an l_ label)
// hashes that atom's contents, since nothing else can name it.
import { createHash } from "crypto";

export interface Atom {
  name: string; // symbol name; anonymous atoms get "<anon>"
  section: string; // "__TEXT,__text"
  code: boolean;
  hash: string;
  refs: string[]; // named symbols this atom refers to (defined or not)
  external: boolean;
  nameRef?: bigint; // for __llvm_prf_data records: MD5 of the PGO name
}

interface Section { seg: string; sect: string; addr: number; size: number; offset: number; reloff: number; nreloc: number; flags: number; }
interface Sym { name: string; type: number; sect: number; value: number; }
interface Reloc { address: number; symbolnum: number; pcrel: boolean; length: number; extern: boolean; type: number; }

const S_CSTRING_LITERALS = 0x2;
const S_ATTR_PURE_INSTRUCTIONS = 0x80000000;

// Profile counters and records are linker-private (l___profc_*) but are the
// link between code and coverage, so they keep their names.
const isAnonName = (n: string) =>
  n === "" || ((n.startsWith("l") || n.startsWith("L")) && !/^l___prof[cd]_/.test(n));

export function readAtoms(buf: Buffer): Atom[] {
  if (buf.readUInt32LE(0) !== 0xfeedfacf || buf.readUInt32LE(12) !== 1)
    throw new Error("not a 64-bit Mach-O object");
  const ncmds = buf.readUInt32LE(16);
  const sections: Section[] = [];
  let syms: Sym[] = [];
  let off = 32;
  for (let i = 0; i < ncmds; i++) {
    const cmd = buf.readUInt32LE(off), size = buf.readUInt32LE(off + 4);
    if (cmd === 0x19) {
      const nsects = buf.readUInt32LE(off + 64);
      for (let s = 0; s < nsects; s++) {
        const p = off + 72 + s * 80;
        const str = (o: number) => buf.toString("latin1", o, o + 16).replace(/\0.*$/, "");
        sections.push({
          sect: str(p), seg: str(p + 16), addr: Number(buf.readBigUInt64LE(p + 32)),
          size: Number(buf.readBigUInt64LE(p + 40)), offset: buf.readUInt32LE(p + 48),
          reloff: buf.readUInt32LE(p + 56), nreloc: buf.readUInt32LE(p + 60), flags: buf.readUInt32LE(p + 64),
        });
      }
    } else if (cmd === 0x2) {
      const symoff = buf.readUInt32LE(off + 8), nsyms = buf.readUInt32LE(off + 12), stroff = buf.readUInt32LE(off + 16);
      syms = Array.from({ length: nsyms }, (_, s) => {
        const e = symoff + s * 16, strx = buf.readUInt32LE(e);
        return {
          name: buf.toString("latin1", stroff + strx, buf.indexOf(0, stroff + strx)),
          type: buf[e + 4], sect: buf[e + 5], value: Number(buf.readBigUInt64LE(e + 8)),
        };
      });
    }
    off += size;
  }

  // Atom boundaries: every defined, non-debug symbol starts one. Literal
  // sections are cut at each NUL instead, since their labels are optional.
  interface Raw { sect: number; start: number; end: number; name: string; external: boolean; }
  const raws: Raw[] = [];
  sections.forEach((sec, i) => {
    const starts = new Map<number, Sym>();
    for (const s of syms)
      if (!(s.type & 0xe0) && (s.type & 0x0e) === 0x0e && s.sect === i + 1) {
        const prev = starts.get(s.value);
        if (!prev || (isAnonName(prev.name) && !isAnonName(s.name))) starts.set(s.value, s);
      }
    if (sec.flags & S_CSTRING_LITERALS && sec.offset) {
      let p = sec.addr;
      while (p < sec.addr + sec.size) {
        const nul = buf.indexOf(0, sec.offset + (p - sec.addr));
        const end = Math.min(sec.addr + sec.size, nul - sec.offset + sec.addr + 1);
        raws.push({ sect: i, start: p, end, name: "", external: false });
        p = end;
      }
      return;
    }
    const sorted = [...starts.values()].sort((a, b) => a.value - b.value);
    if (!sorted.length || sorted[0].value > sec.addr) sorted.unshift({ name: "", type: 0, sect: i + 1, value: sec.addr });
    sorted.forEach((s, k) => {
      const end = k + 1 < sorted.length ? sorted[k + 1].value : sec.addr + sec.size;
      if (end > s.value || sec.size === 0)
        raws.push({ sect: i, start: s.value, end, name: isAnonName(s.name) ? "" : s.name, external: !!(s.type & 1) });
    });
  });
  raws.sort((a, b) => a.sect - b.sect || a.start - b.start);
  const atomAt = (sect: number, addr: number) => {
    let lo = 0, hi = raws.length - 1, best = -1;
    while (lo <= hi) {
      const mid = (lo + hi) >> 1, r = raws[mid];
      if (r.sect < sect || (r.sect === sect && r.start <= addr)) { best = mid; lo = mid + 1; } else hi = mid - 1;
    }
    return best >= 0 && raws[best].sect === sect ? best : -1;
  };

  const relocsOf = (sec: Section): Reloc[] =>
    Array.from({ length: sec.nreloc }, (_, r) => {
      const p = sec.reloff + r * 8, w = buf.readUInt32LE(p + 4);
      return { address: buf.readInt32LE(p), symbolnum: w & 0xffffff, pcrel: !!((w >> 24) & 1), length: (w >> 25) & 3, extern: !!((w >> 27) & 1), type: w >>> 28 };
    });
  const byAtom = new Map<number, Reloc[]>();
  sections.forEach((sec, i) => {
    for (const r of relocsOf(sec)) {
      const a = atomAt(i, sec.addr + r.address);
      if (a >= 0) (byAtom.get(a) ?? byAtom.set(a, []).get(a)!).push(r);
    }
  });

  // A target is either a name (stable across edits) or, for anonymous
  // atoms, their content hash, computed on demand and memoized.
  const contentHash = new Map<number, string>();
  const named = new Map<number, string[]>();
  const hashAtom = (a: number, depth = 0): string => {
    const memo = contentHash.get(a);
    if (memo) return memo;
    if (depth > 64) return "deep";
    const r = raws[a], sec = sections[r.sect];
    const h = createHash("sha1");
    if (sec.offset) h.update(buf.subarray(sec.offset + (r.start - sec.addr), sec.offset + (r.end - sec.addr)));
    else h.update(`zerofill:${r.end - r.start}`);
    const refs: string[] = [];
    for (const rel of byAtom.get(a) ?? []) {
      let target: string;
      if (rel.extern) {
        const s = syms[rel.symbolnum];
        const defined = (s.type & 0x0e) === 0x0e;
        if (!defined || !isAnonName(s.name)) { target = s.name; refs.push(s.name); }
        else {
          const t = atomAt(s.sect - 1, s.value);
          target = t === a ? "self" : `@${hashAtom(t, depth + 1)}+${s.value - raws[t].start}`;
        }
      } else if (rel.type === 10 /* ARM64_RELOC_ADDEND */) {
        target = `addend:${rel.symbolnum}`;
      } else {
        target = `sect:${rel.symbolnum}`;
      }
      h.update(`|${rel.address - (r.start - sec.addr)},${rel.type},${rel.pcrel},${rel.length},${target}`);
    }
    const d = h.digest("hex");
    contentHash.set(a, d);
    named.set(a, refs);
    return d;
  };

  return raws.map((r, a) => {
    const sec = sections[r.sect];
    const hash = hashAtom(a);
    return {
      name: r.name || "<anon>", section: `${sec.seg},${sec.sect}`,
      code: !!(sec.flags & S_ATTR_PURE_INSTRUCTIONS), hash, refs: named.get(a) ?? [], external: r.external,
      nameRef: sec.sect === "__llvm_prf_data" && r.name ? buf.readBigUInt64LE(sec.offset + (r.start - sec.addr)) : undefined,
    };
  });
}

