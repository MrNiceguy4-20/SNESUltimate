from __future__ import annotations
import argparse, json, pathlib, re, sys
HEX = set("0123456789abcdef")
def load(path: pathlib.Path, label: str) -> dict:
    if not path.is_file():
        raise ValueError(f"{label} report not found: {path}")
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        raise ValueError(f"{label} report is not valid JSON: {exc}") from exc
    if not isinstance(data, dict):
        raise ValueError(f"{label} report root must be an object")
    return data
def require_zero_failures(report: dict, label: str) -> None:
    failed = int(report.get("failed", -1))
    cases = int(report.get("cases", report.get("executed", -1)))
    passed = int(report.get("passed", -1))
    if failed != 0:
        raise ValueError(f"{label}: {failed} failure(s)")
    if cases <= 0 or passed != cases:
        raise ValueError(f"{label}: incomplete totals passed={passed} cases={cases}")
def check_65816(report: dict) -> dict:
    require_zero_failures(report, "65816")
    if int(report.get("sample_per_file", -1)) != 0:
        raise ValueError("65816: sampled report is not sufficient; rerun with --sample 0 / SNESULTIMATE_EXTERNAL_SAMPLE=0")
    files = report.get("files")
    if not isinstance(files, list):
        raise ValueError("65816: missing per-file results")
    seen = set()
    bad_selected = []
    for item in files:
        name = pathlib.Path(str(item.get("file", ""))).name.lower()
        m = re.fullmatch(r"([0-9a-f]{2})\.(e|n)\.json", name)
        if not m:
            continue
        seen.add((int(m.group(1), 16), m.group(2)))
        if int(item.get("selected", 0)) != int(item.get("parsed", -1)):
            bad_selected.append(name)
        if int(item.get("failed", 0)) != 0 or int(item.get("passed", -1)) != int(item.get("selected", -2)):
            raise ValueError(f"65816: file not clean: {name}")
    expected = {(op, mode) for op in range(256) for mode in ("e", "n")}
    missing = sorted(expected - seen)
    if missing:
        preview = ", ".join(f"{op:02x}.{mode}.json" for op, mode in missing[:8])
        raise ValueError(f"65816: missing {len(missing)} opcode/mode files (e.g. {preview})")
    if bad_selected:
        raise ValueError(f"65816: {len(bad_selected)} files were not exhaustively selected")
    if int(report.get("files_total", len(files))) < 512:
        raise ValueError("65816: files_total is below 512")
    return {"files": 512, "cases": int(report["cases"])}
def check_spc700(report: dict) -> dict:
    require_zero_failures(report, "SPC700")
    if int(report.get("sample_per_file", -1)) != 0:
        raise ValueError("SPC700: sampled report is not sufficient; rerun with --sample 0 / SNESULTIMATE_EXTERNAL_SAMPLE=0")
    files = report.get("files")
    if not isinstance(files, list):
        raise ValueError("SPC700: missing per-file results")
    seen = set()
    for item in files:
        name = pathlib.Path(str(item.get("file", ""))).name.lower()
        m = re.fullmatch(r"([0-9a-f]{2})\.json", name)
        if not m:
            continue
        seen.add(int(m.group(1), 16))
        if int(item.get("selected", 0)) != int(item.get("parsed", -1)):
            raise ValueError(f"SPC700: file was sampled/truncated: {name}")
        if int(item.get("failed", 0)) != 0 or int(item.get("passed", -1)) != int(item.get("selected", -2)):
            raise ValueError(f"SPC700: file not clean: {name}")
    missing = sorted(set(range(256)) - seen)
    if missing:
        preview = ", ".join(f"{op:02x}.json" for op in missing[:8])
        raise ValueError(f"SPC700: missing {len(missing)} opcode files (e.g. {preview})")
    if int(report.get("files_total", len(files))) < 256:
        raise ValueError("SPC700: files_total is below 256")
    return {"files": 256, "cases": int(report["cases"])}
def check_hardware(report: dict) -> dict:
    if int(report.get("failed", -1)) != 0:
        raise ValueError(f"hardware ROMs: {report.get('failed')} failure(s)")
    cases = report.get("cases")
    if not isinstance(cases, list):
        raise ValueError("hardware ROMs: missing case list")
    required = {
        "gilyon 65C816 full conformance": False,
        "gilyon SPC700 conformance": False,
    }
    for case in cases:
        name = str(case.get("name", ""))
        if name in required:
            if case.get("skipped") or not case.get("passed"):
                raise ValueError(f"hardware ROMs: required case did not pass: {name}")
            required[name] = True
    missing = [name for name, ok in required.items() if not ok]
    if missing:
        raise ValueError("hardware ROMs: missing required gilyon case(s): " + ", ".join(missing))
    return {"required_cases": len(required), "executed": int(report.get("executed", 0))}
def check_rom(report: dict) -> dict:
    failed = int(report.get("failed", -1))
    if failed != 0:
        raise ValueError(f"ROM regressions: {failed} failure(s)")
    executed = int(report.get("executed", 0))
    passed = int(report.get("passed", -1))
    if executed <= 0 or passed != executed:
        raise ValueError(f"ROM regressions: incomplete totals passed={passed} executed={executed}")
    return {"executed": executed}
def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--65816-report", required=True, type=pathlib.Path)
    ap.add_argument("--spc700-report", required=True, type=pathlib.Path)
    ap.add_argument("--hardware-report", required=True, type=pathlib.Path)
    ap.add_argument("--rom-report", type=pathlib.Path, help="optional additional golden ROM regression report to require")
    ap.add_argument("--output-json", type=pathlib.Path)
    args = ap.parse_args()
    summary = {"format": 1, "phase": "99-to-100", "passed": False, "checks": {}}
    try:
        summary["checks"]["65816"] = check_65816(load(args.__dict__["65816_report"], "65816"))
        summary["checks"]["spc700"] = check_spc700(load(args.spc700_report, "SPC700"))
        summary["checks"]["hardware"] = check_hardware(load(args.hardware_report, "hardware"))
        if args.rom_report:
            summary["checks"]["rom"] = check_rom(load(args.rom_report, "ROM regression"))
        summary["passed"] = True
        summary["verdict"] = "external conformance gate passed"
    except ValueError as exc:
        summary["verdict"] = str(exc)
        if args.output_json:
            args.output_json.parent.mkdir(parents=True, exist_ok=True)
            args.output_json.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print("FINAL CONFORMANCE GATE: FAIL - " + str(exc), file=sys.stderr)
        return 1
    if args.output_json:
        args.output_json.parent.mkdir(parents=True, exist_ok=True)
        args.output_json.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("FINAL CONFORMANCE GATE: PASS")
    print(f"  65816: {summary['checks']['65816']['cases']} exhaustive vectors across 512 opcode/mode files")
    print(f"  SPC700: {summary['checks']['spc700']['cases']} exhaustive vectors across 256 opcode files")
    print("  hardware: gilyon 65C816 + SPC700 conformance ROMs passed")
    return 0
if __name__ == "__main__":
    raise SystemExit(main())
