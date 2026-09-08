#!/usr/bin/env python3
"""Drive CBMC programmatically through its --json-ui interface.

This is the API to reach for first: no build, no linking, stable across
versions, and it works with every tool in the family (goto-instrument,
goto-analyzer, ... all accept --json-ui too).

    ./drive_json.py ../examples/04_memory.c --pointer-check --unwind 6
"""
from __future__ import annotations

import json
import subprocess
import sys
from dataclasses import dataclass

# cbmc's documented exit codes
EXIT_SUCCESS = 0
EXIT_VERIFICATION_FAILED = 10
EXIT_PARSE_ERROR = 6


@dataclass
class Property:
    id: str
    description: str
    status: str
    file: str
    line: str
    function: str
    trace: list


def run_cbmc(args: list[str]) -> tuple[int, list]:
    """Run cbmc and return (exit code, parsed JSON elements)."""
    proc = subprocess.run(
        ["cbmc", "--json-ui", *args],
        capture_output=True,
        text=True,
    )
    # --json-ui writes a single JSON array to stdout; diagnostics from the
    # driver itself (bad flags, missing files) still go to stderr as text.
    try:
        elements = json.loads(proc.stdout)
    except json.JSONDecodeError:
        print(proc.stdout, file=sys.stderr)
        print(proc.stderr, file=sys.stderr)
        raise SystemExit(f"cbmc produced no JSON (exit {proc.returncode})")
    return proc.returncode, elements


def properties(elements: list) -> list[Property]:
    out = []
    for element in elements:
        for result in element.get("result", []):
            loc = result.get("sourceLocation", {})
            out.append(
                Property(
                    id=result.get("property", "?"),
                    description=result.get("description", ""),
                    status=result.get("status", "UNKNOWN"),
                    file=loc.get("file", "?"),
                    line=loc.get("line", "?"),
                    function=loc.get("function", "?"),
                    trace=result.get("trace", []),
                )
            )
    return out


def status(elements: list) -> str:
    """The final cProverStatus element: success | failure | error."""
    for element in reversed(elements):
        if "cProverStatus" in element:
            return element["cProverStatus"]
    return "unknown"


def errors(elements: list) -> list[str]:
    return [
        e["messageText"]
        for e in elements
        if e.get("messageType") == "ERROR" and "messageText" in e
    ]


def assignments(trace: list) -> list[str]:
    """Pull the concrete values out of a counterexample trace.

    CBMC's model also fixes its own bookkeeping symbols (__CPROVER_*,
    goto_symex$$*) and emits an initial zero-assignment for each variable
    before the interesting one; both are noise for a human reader.
    """
    latest: dict[str, str] = {}
    for step in trace:
        if step.get("stepType") != "assignment":
            continue
        if step.get("assignmentType") != "variable":
            continue
        lhs = step.get("lhs")
        if not lhs or lhs.startswith("__CPROVER_") or "goto_symex" in lhs:
            continue
        value = step.get("value", {})
        latest[lhs] = value.get("data") or value.get("binary") or json.dumps(value)
    return [f"{lhs} = {value}" for lhs, value in latest.items()]


def main(argv: list[str]) -> int:
    if not argv:
        print(__doc__)
        return 2

    code, elements = run_cbmc([*argv, "--trace"])

    for message in errors(elements):
        print(f"error: {message}", file=sys.stderr)

    props = properties(elements)
    failed = [p for p in props if p.status == "FAILURE"]

    for p in props:
        mark = {"SUCCESS": "ok  ", "FAILURE": "FAIL"}.get(p.status, p.status)
        print(f"{mark}  {p.id}\n        {p.file}:{p.line} ({p.function}) {p.description}")

    for p in failed:
        print(f"\ncounterexample for {p.id}:")
        for line in assignments(p.trace):
            print(f"    {line}")

    print(f"\n{len(props) - len(failed)}/{len(props)} passed - {status(elements)}")
    print(f"cbmc exit code: {code}"
          f" ({'pass' if code == EXIT_SUCCESS else 'fail' if code == EXIT_VERIFICATION_FAILED else 'error'})")
    return code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
