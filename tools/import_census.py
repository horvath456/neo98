#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Which exports the collection really imports and which ones it really calls.

PLAN.md step 5 read the import tables of the EXE files. A packed program shows
one alibi import per DLL there and resolves the rest through GetProcAddress at
run time, so step 5 never saw it; Dev-C++ ended silently because two of
the 580 names its UPX loader asked for answered null. This census measures the
run instead of the file.

It starts every program of the collection through tools/smoke_matrix.py in the
measuring mode (`--census`): GetProcAddress writes
every name it is asked for with its answer, every bound import is written at the
end with the number of calls it took, and a call of an export this port does not
implement writes a line and returns the answer of an unimplemented Win98 export
instead of stopping the run, so one start shows every such call of a program.

    build/analysis-venv/bin/python tools/import_census.py --limit 40
    build/analysis-venv/bin/python tools/import_census.py --jobs 4

Writes build/import-census/census.json.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tools"))

import smoke_matrix  # noqa: E402

IMPORT = re.compile(r"^PROBE_CENSUS_IMPORT dll=([0-9a-f]*) name=([0-9a-f]*) kind=(\S+) calls=(\d+)$",
                    re.M)
LOOKUP_NAME = re.compile(r"^PROBE_CENSUS_LOOKUP module=(\S+) name=(\S+) result=([0-9a-f]+)$", re.M)
LOOKUP_ORDINAL = re.compile(r"^PROBE_CENSUS_LOOKUP module=(\S+) ordinal=(\d+) result=([0-9a-f]+)$",
                            re.M)
CALL = re.compile(r"^PROBE_CENSUS_CALL dll=(\S+) name=(\S+) from=(.*?) args=", re.M)
MISSING_IMPORT = re.compile(r"^MISSING IMPORT (\S+)!(\S+) needed by (.*)$", re.M)
NO_DLL = re.compile(r"^DLL dependency load failed (\S+) error=(\d+)$", re.M)

# A packer the static import analysis could not see through. The section names
# are the ones the packers write; an image whose only sections have no name at
# all and one import per DLL is packed by something this list does not know.
PACKERS = (
    ("UPX", ("UPX0", "UPX1", "UPX2", "UPX!")),
    ("ASPack", (".aspack", ".adata")),
    ("Petite", (".petite", "petite")),
    ("PECompact", ("pec1", "pec2", "PEC2")),
    ("NeoLite", (".neolit", "neolite")),
    ("WWPack", (".WWP32", ".WWPACK")),
    ("Shrinker", (".shrink",)),
    ("PKLite32", ("pklstb", ".pklstb")),
)


def unhex(text):
    try:
        return bytes.fromhex(text).decode("cp1252", "replace")
    except ValueError:
        return ""


def packing(path):
    """The packer of one image and the size of its static import table."""
    try:
        import pefile
        image = pefile.PE(str(path), fast_load=True)
    except Exception:
        return {"packer": None, "sections": [], "static_imports": 0, "import_dlls": 0}
    names, imports, dlls = [], 0, 0
    try:
        for section in image.sections:
            names.append(section.Name.rstrip(b"\0").decode("latin1", "replace"))
        image.parse_data_directories(
            directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]])
        for module in getattr(image, "DIRECTORY_ENTRY_IMPORT", []):
            dlls += 1
            imports += len(module.imports)
    except Exception:
        pass
    finally:
        image.close()
    packer = None
    for label, marks in PACKERS:
        if any(name in marks for name in names):
            packer = label
            break
    if packer is None and dlls and imports <= dlls + 1 and all(not name for name in names):
        packer = "unknown"
    elif packer is None and dlls and imports <= dlls + 1:
        packer = "probably packed"
    return {"packer": packer, "sections": names, "static_imports": imports, "import_dlls": dlls}


def read_log(path):
    """One program's census out of its matrix log."""
    text = Path(path).read_text(errors="replace")
    imports, calls, lookups = {}, {}, []
    for dll, name, kind, count in IMPORT.findall(text):
        key = (unhex(dll).upper(), unhex(name))
        row = imports.setdefault(key, {"kind": kind, "calls": 0})
        row["calls"] += int(count)
        # A process and its children write their own block; the class is the
        # same, and the calls add up.
        row["kind"] = kind
    for dll, name, _ in CALL.findall(text):
        calls[(dll.upper(), name)] = calls.get((dll.upper(), name), 0) + 1
    for module, name, result in LOOKUP_NAME.findall(text):
        lookups.append({"module": module.upper(), "name": name, "found": result != "00000000"})
    for module, ordinal, result in LOOKUP_ORDINAL.findall(text):
        lookups.append({"module": module.upper(), "ordinal": int(ordinal),
                        "found": result != "00000000"})
    return {"imports": imports, "calls": calls, "lookups": lookups,
            "missing_imports": sorted({f"{dll}!{name}" for dll, name, _ in MISSING_IMPORT.findall(text)}),
            "missing_dlls": sorted({dll for dll, _ in NO_DLL.findall(text)})}


def census(results, contract):
    """Import and call counts over every program of the run."""
    classes, detail = {}, {}
    for module in contract["modules"]:
        for row in module["exports"]:
            for name in row["names"] or [f"ordinal_{row['ordinal']}"]:
                classes[(module["dll"], name)] = row["kind"]
                if row["kind"] == "missing":
                    detail[f"{module['dll']}!{name}"] = {
                        "arguments": row["argument_dwords"],
                        "body": "16-bit thunk" if row.get("thunk16") else "32-bit code"}
    imported, called, kinds = defaultdict(set), defaultdict(set), {}
    dll_missing, per_program = defaultdict(set), []
    lookup_null = defaultdict(set)
    for row in results["programs"]:
        if not row.get("log"):
            continue
        path = ROOT / row["log"] if not row["log"].startswith("~") else Path(row["log"]).expanduser()
        if not path.exists():
            continue
        data = read_log(path)
        program = {"id": row["id"], "class": row["class"], "detail": row.get("detail", ""),
                   "missing_calls": [], "missing_lookups": []}
        for (dll, name), item in data["imports"].items():
            key = f"{dll}!{name}"
            imported[key].add(row["id"])
            kinds[key] = classes.get((dll, name), item["kind"])
            if item["calls"]:
                called[key].add(row["id"])
                if item["kind"] == "missing":
                    program["missing_calls"].append({"export": key, "calls": item["calls"]})
        for entry in data["lookups"]:
            if entry["found"]:
                continue
            name = entry.get("name") or f"ordinal_{entry['ordinal']}"
            key = f"{entry['module']}!{name}"
            lookup_null[key].add(row["id"])
            program["missing_lookups"].append(key)
        for name in data["missing_imports"]:
            lookup_null[name].add(row["id"])
        for dll in data["missing_dlls"]:
            dll_missing[dll].add(row["id"])
        program["missing_calls"].sort(key=lambda item: -item["calls"])
        program["missing_lookups"] = sorted(set(program["missing_lookups"]))
        program["packed"] = row.get("packed")
        if program["missing_calls"] or program["missing_lookups"]:
            per_program.append(program)
    exports = []
    for key in sorted(set(imported) | set(lookup_null)):
        exports.append({"export": key, "class": kinds.get(key, "unknown"),
                        "imported_by": len(imported.get(key, ())),
                        "called_by": len(called.get(key, ())),
                        "null_for": len(lookup_null.get(key, ())),
                        "examples": sorted(called.get(key) or imported.get(key) or
                                           lookup_null.get(key, ()))[:5]})
    exports.sort(key=lambda row: (-row["called_by"], -row["imported_by"], row["export"]))
    for row in exports:
        row.update(detail.get(row["export"], {}))
    return {"exports": exports,
            "missing_dlls": sorted(({"dll": dll, "programs": len(ids),
                                     "examples": sorted(ids)[:5]} for dll, ids in dll_missing.items()),
                                   key=lambda row: -row["programs"]),
            "programs": sorted(per_program, key=lambda row: row["id"])}


# The census report work list is kept by hand: it says what became of
# every export the collection binds. A new census run must not throw it away, so
# the generator reads the block back out of the document and writes it again.
LEGACY_WORKLIST_BEGIN = "<!-- Arbeitsliste, von Hand gepflegt: bleibt bei jedem Lauf stehen -->"
LEGACY_WORKLIST_END = "<!-- Ende der Arbeitsliste -->"

WORKLIST_BEGIN = "<!-- Manually maintained work list; preserved across runs -->"
WORKLIST_END = "<!-- End of work list -->"


def worklist(document):
    if not document.exists():
        return []
    text = document.read_text()
    text = text.replace(LEGACY_WORKLIST_BEGIN, WORKLIST_BEGIN).replace(LEGACY_WORKLIST_END, WORKLIST_END)
    start, end = text.find(WORKLIST_BEGIN), text.find(WORKLIST_END)
    if start < 0 or end < start:
        return []
    return text[start:end + len(WORKLIST_END)].rstrip("\n").split("\n") + [""]


def markdown(report, document):
    counts = report["counts"]
    lines = ["# Runtime import census", "",
             "Generated by `tools/import_census.py` from observed imports and calls.", "",
             "## Coverage", "",
             f"- Started {counts['programs']} programs; {counts['measured']} produced census data.",
             f"- {counts['packed']} programs use packers, which can hide imports from static inspection.",
             f"- Bound {counts['exports']} distinct exports; called {counts['called']} at least once.",
             f"- Called {counts['missing_called']} exports classified as missing.", ""]
    lines += worklist(document)
    lines += ["## Most frequently called missing exports", "",
              "N counts importing programs; M counts calling programs. Sorted by M, then N.", "",
              "| Export | Class | N | M | Example |", "| --- | --- | ---: | ---: | --- |"]
    missing = [row for row in report["exports"] if row["class"] == "missing"]
    for row in missing[:60]:
        lines.append(f"| {row['export']} | {row['class']} | {row['imported_by']} | "
                     f"{row['called_by']} | {row['examples'][0] if row['examples'] else ''} |")
    if not missing:
        lines.append("| (none) | | 0 | 0 | |")
    lines += ["", "## Most frequent exports", "",
              "| Export | Class | N | M |", "| --- | --- | ---: | ---: |"]
    for row in report["exports"][:40]:
        lines.append(f"| {row['export']} | {row['class']} | {row['imported_by']} | {row['called_by']} |")
    nulls = [row for row in report["exports"] if row["null_for"]]
    nulls.sort(key=lambda row: -row["null_for"])
    lines += ["", "## Unresolved names", "",
              "Names absent from the export contract or loaded DLLs.", "",
              "| Name | Programs | Example |", "| --- | ---: | --- |"]
    for row in nulls[:40]:
        lines.append(f"| {row['export']} | {row['null_for']} | "
                     f"{row['examples'][0] if row['examples'] else ''} |")
    if not nulls:
        lines.append("| (none) | 0 | |")
    lines += ["", "## Missing DLLs", "", "| DLL | Programs | Example |", "| --- | ---: | --- |"]
    for row in report["missing_dlls"][:30]:
        lines.append(f"| {row['dll']} | {row['programs']} | {row['examples'][0]} |")
    if not report["missing_dlls"]:
        lines.append("| (none) | 0 | |")
    lines += ["", "## Programs calling missing functions", "",
              "| Program | Class | Packer | Missing calls |", "| --- | --- | --- | --- |"]
    for row in report["programs"][:60]:
        if not row["missing_calls"]:
            continue
        names = ", ".join(item["export"] for item in row["missing_calls"][:6])
        lines.append(f"| {row['id']} | {row['class']} | {row.get('packed') or '-'} | {names} |")
    lines += ["", "## Implementation candidates", "",
              "Arguments are DWORD counts; `?` means unknown. A 16-bit thunk delegates to",
              "a 16-bit provider. Counts N and M have the same meaning as above.", "",
              "| Export | Arguments | Implementation | N | M | Examples |",
              "| --- | ---: | --- | ---: | ---: | --- |"]
    for row in missing[:40]:
        count = row.get("arguments", 256)
        lines.append(f"| {row['export']} | {'?' if count == 256 else count} | "
                     f"{row.get('body', '?')} | {row['imported_by']} | {row['called_by']} | "
                     + ", ".join(row["examples"][:2]) + " |")
    lines += ["", "## Limits", "",
              "- Only startup without user input is measured; later menu or save operations are absent.",
              "- Census mode continues after missing exports with result zero and last error 120.",
              "  Normal execution stops at such a call.",
              "- A program that crashes contributes only calls observed before the crash.", ""]
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--out", type=Path, default=ROOT / "build/import-census")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--limit", type=int, help="only this many programs, for a trial run")
    parser.add_argument("--only", help="regular expression on the program id")
    parser.add_argument("--collection", action="append", choices=("games", "windows"))
    parser.add_argument("--time-limit", type=int, default=20)
    parser.add_argument("--build", action="store_true", help="relink the runner before the run")
    parser.add_argument("--results", type=Path,
                        help="read an existing results.json instead of starting the collection")
    args = parser.parse_args(argv)
    args.out.mkdir(parents=True, exist_ok=True)
    matrix = args.out / "matrix"
    if args.results:
        results = json.loads(args.results.read_text())
    else:
        arguments = ["--census", "--out", str(matrix), "--jobs", str(args.jobs),
                     "--time-limit", str(args.time_limit), "--no-compare"]
        if args.build:
            arguments.append("--build")
        if args.limit:
            arguments += ["--limit", str(args.limit)]
        if args.only:
            arguments += ["--only", args.only]
        for name in args.collection or []:
            arguments += ["--collection", name]
        smoke_matrix.main(arguments)
        results = json.loads((matrix / "results.json").read_text())
    for row in results["programs"]:
        if row.get("class") == "skipped" or not row.get("path"):
            continue
        path = Path(row["path"]).expanduser() if row["path"].startswith("~") else ROOT / row["path"]
        row["packed"] = packing(path)["packer"] if path.exists() else None
    contract = json.loads((ROOT / "build/export-contract/report.json").read_text())
    report = census(results, contract)
    started = [row for row in results["programs"] if row.get("class") != "skipped"]
    report["counts"] = {
        "programs": len(started),
        "measured": sum(1 for row in started if row.get("log")),
        "packed": sum(1 for row in started if row.get("packed")),
        "exports": len(report["exports"]),
        "called": sum(1 for row in report["exports"] if row["called_by"]),
        "missing_called": sum(1 for row in report["exports"]
                              if row["class"] == "missing" and row["called_by"]),
    }
    report["revision"] = results.get("revision", "unknown")
    report["collections"] = results.get("collections", [])
    report["packers"] = dict(Counter(row.get("packed") for row in started if row.get("packed")))
    (args.out / "census.json").write_text(json.dumps(report, indent=1, ensure_ascii=True) + "\n")
    document = args.out / "census.md"
    document.write_text(markdown(report, document))
    print(json.dumps(report["counts"], sort_keys=True))
    for row in [item for item in report["exports"] if item["class"] == "missing"][:20]:
        print(f"{row['called_by']:4d} calls {row['imported_by']:4d} imports  {row['export']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
