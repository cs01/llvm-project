// Reading and appending to ninja's deps log (.ninja_deps, format version 4).
//
// Ninja rebuilds an output whose mtime is newer than the mtime stored with its
// deps record ("stored deps info out of date"), so marking an object current
// takes a new deps record carrying the object's new mtime. Ninja itself only
// ever appends to this file and the last record for an output wins.
//
// Layout: "# ninjadeps\n", int32 version, then records, each a uint32 size
// followed by a payload. A size with the high bit set is a deps record:
// int32 output id, uint32 mtime low, uint32 mtime high (nanoseconds), then
// int32 input ids. Otherwise it is a path record: the path padded with NULs to
// a multiple of 4, then uint32 ~id; ids count up from 0 in file order.
import { appendFileSync, readFileSync, statSync } from "fs";

const MAGIC = "# ninjadeps\n";
const VERSION = 4;

export type DepsLog = {
  file: string;
  ids: Map<string, number>;
  // Latest deps record per output id: its input ids.
  deps: Map<number, number[]>;
};

export function readDepsLog(file: string): DepsLog {
  const buf = readFileSync(file);
  if (buf.toString("latin1", 0, MAGIC.length) !== MAGIC || buf.readInt32LE(MAGIC.length) !== VERSION)
    throw new Error(`${file}: not a version ${VERSION} ninja deps log`);
  const ids = new Map<string, number>(), deps = new Map<number, number[]>();
  let off = MAGIC.length + 4, nextId = 0;
  while (off + 4 <= buf.length) {
    const raw = buf.readUInt32LE(off);
    const size = raw & 0x7fffffff;
    off += 4;
    if (off + size > buf.length) break; // truncated tail, as ninja tolerates
    if (raw & 0x80000000) {
      const out = buf.readInt32LE(off);
      const inputs: number[] = [];
      for (let p = off + 12; p < off + size; p += 4) inputs.push(buf.readInt32LE(p));
      deps.set(out, inputs);
    } else {
      let end = off + size - 4;
      while (end > off && buf[end - 1] === 0) end--;
      ids.set(buf.toString("utf8", off, end), nextId++);
    }
    off += size;
  }
  return { file, ids, deps };
}

/// Re-records the latest deps of each output with its current mtime on disk,
/// so ninja treats the output as up to date with respect to its deps record.
export function refreshDeps(log: DepsLog, outputs: string[], dir: string) {
  const records: Buffer[] = [];
  for (const out of outputs) {
    const id = log.ids.get(out);
    const inputs = id === undefined ? undefined : log.deps.get(id);
    if (id === undefined || !inputs) throw new Error(`${out}: no deps record to refresh`);
    const mtime = statSync(`${dir}/${out}`, { bigint: true }).mtimeNs;
    const rec = Buffer.alloc(4 + 12 + 4 * inputs.length);
    rec.writeUInt32LE(((12 + 4 * inputs.length) | 0x80000000) >>> 0, 0);
    rec.writeInt32LE(id, 4);
    rec.writeUInt32LE(Number(mtime & 0xffffffffn), 8);
    rec.writeUInt32LE(Number(mtime >> 32n), 12);
    inputs.forEach((x, i) => rec.writeInt32LE(x, 16 + 4 * i));
    records.push(rec);
  }
  appendFileSync(log.file, Buffer.concat(records));
}

/// Ninja also compares inputs against the output mtime recorded in its build
/// log (.ninja_log, "start end mtime path command-hash" per line, last entry
/// wins), so the kept outputs get fresh entries there too, with the same
/// command hash so they are not considered changed.
export function refreshBuildLog(dir: string, outputs: string[]) {
  const file = `${dir}/.ninja_log`;
  const lines = readFileSync(file, "utf8").split("\n");
  if (lines[0] !== "# ninja log v7") throw new Error(`${file}: expected ninja log v7`);
  const last = new Map<string, string[]>();
  const want = new Set(outputs);
  for (const l of lines) {
    const f = l.split("\t");
    if (f.length === 5 && want.has(f[3])) last.set(f[3], f);
  }
  const out: string[] = [];
  for (const o of outputs) {
    const f = last.get(o);
    if (!f) throw new Error(`${o}: no build log entry to refresh`);
    const mtime = statSync(`${dir}/${o}`, { bigint: true }).mtimeNs;
    out.push([f[0], f[1], mtime.toString(), o, f[4]].join("\t"));
  }
  appendFileSync(file, out.join("\n") + "\n");
}
