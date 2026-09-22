#!/usr/bin/env python3
"""Check that the NullabilitySafety SSAF summaries carry the same evidence as
the -Rnullsafe-evidence remarks for one translation unit.

Compares sets of (entity, polarity), where polarity is nonnull, nullable or
allret. Summary entities are located through the EntitySourceLocations summary
extracted alongside. Each remark names the declaration it saw ("declared at",
or the remark's own location for "always returns"), which may be any
redeclaration; it is resolved to the summary entity of the same polarity with
a location on that line whose column is the greatest at or before the
remark's (entities are located at their start, remarks at the name). Remarks
in files with no entity locations at all (system headers) are skipped and
counted.

Usage: nullability-ssaf-parity.py <remarks.txt> <tu-summary.json>
Exits nonzero and prints the differences if the sets differ.
"""
import collections
import json
import os
import re
import sys

REMARK = re.compile(
    r"^(?P<file>\S+?):(?P<line>\d+):(?P<col>\d+): remark: (?P<msg>.*) "
    r"\[-Rnullsafe-evidence\]$")
DECLARED = re.compile(r"\(declared at (?P<file>.+):(?P<line>\d+):(?P<col>\d+)\)")


def remark_keys(path):
    keys = set()
    for line in open(path):
        m = REMARK.match(line.strip())
        if not m:
            continue
        msg = m.group("msg")
        if "always returns a non-null pointer" in msg:
            keys.add(((m.group("file"), int(m.group("line")), int(m.group("col"))),
                      "allret"))
            continue
        d = DECLARED.search(msg)
        if not d:
            continue
        polarity = "nonnull" if re.search(r"\bnonnull\b", msg) else "nullable"
        keys.add(((d.group("file"), int(d.group("line")), int(d.group("col"))),
                  polarity))
    return keys


def summary_keys(path):
    data = json.load(open(path))
    by_name = {s["summary_name"]: s["summary_data"] for s in data["data"]}
    locs = {}
    for entry in by_name.get("EntitySourceLocations", []):
        locs[entry["entity_id"]] = [
            (l["file_path"], l["line"], l["column"])
            for l in entry["entity_summary"]["decl_locations"]]
    entries = set()
    for entry in by_name.get("NullabilitySafety", []):
        s = entry["entity_summary"]
        for field, polarity in (("NonnullEvidence", "nonnull"),
                                ("NullableEvidence", "nullable"),
                                ("AllReturnsNonnull", "allret")):
            for ref, _level in s[field]:
                entries.add((ref["@"], polarity))
    return entries, locs


def main():
    remarks = remark_keys(sys.argv[1])
    entries, locs = summary_keys(sys.argv[2])
    at_line = collections.defaultdict(list)
    for eid, pol in entries:
        for f, line, col in locs.get(eid, []):
            at_line[(os.path.basename(f), line, pol)].append((col, eid))
    located_files = {os.path.basename(f)
                     for ls in locs.values() for f, _l, _c in ls}
    matched = set()
    missing = []
    skipped = 0
    for (f, line, col), pol in sorted(remarks):
        base = os.path.basename(f)
        if base not in located_files:
            skipped += 1
            continue
        cands = [c for c in at_line.get((base, line, pol), []) if c[0] <= col]
        if cands:
            matched.add((max(cands)[1], pol))
        else:
            missing.append(((base, line, col), pol))
    unlocated = {e for e in entries if not locs.get(e[0])}
    extra = sorted(e for e in entries if e not in matched and e not in unlocated)
    print(f"remarks {len(remarks)} ({skipped} skipped: no entity locations in "
          f"file), summary entries {len(entries)} ({len(unlocated)} without a "
          f"location), missing from summaries {len(missing)}, not in remarks "
          f"{len(extra)}")
    for m in missing[:20]:
        print("  missing:", m)
    for e in extra[:20]:
        print("  extra:", e, locs.get(e[0]))
    return 1 if missing or extra else 0


if __name__ == "__main__":
    sys.exit(main())
