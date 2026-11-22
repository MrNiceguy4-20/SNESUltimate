import argparse
import copy
import hashlib
import json
import pathlib
import re
import subprocess
import sys
def parse_test_catalog(path: pathlib.Path) -> dict[int, str]:
    tests = {}
    current = None
    details = []
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = re.match(r"^Test\s+([0-9A-Fa-f]{1,4}):\s*(.*)$", raw)
        if match:
            if current is not None:
                tests[current] = " | ".join(details)
            current = int(match.group(1), 16)
            details = [match.group(2).strip()]
        elif current is not None and raw.strip():
            details.append(raw.strip())
    if current is not None:
        tests[current] = " | ".join(details)
    return tests
def find_wram_watch(result: dict, offset: int):
    for item in result.get("wram_u16_watches", []):
        if int(item.get("offset", -1)) == offset:
            return item
    return None
def sha256_file(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()
def resolved_path(base: pathlib.Path, value: str) -> pathlib.Path:
    path = pathlib.Path(value)
    return path if path.is_absolute() else (base / path).resolve()
def build_command(runner: pathlib.Path, case: dict, rom: pathlib.Path) -> list[str]:
    cmd = ([sys.executable, str(runner)] if runner.suffix.lower() == ".py" else [str(runner)]) + ["--json"]
    if "cycles" in case:
        cmd += ["--cycles", str(case["cycles"])]
    else:
        cmd += ["--frames", str(case.get("frames", 60))]
    if "oracle_wram" in case:
        cmd += ["--oracle-wram", str(case["oracle_wram"])]
    if "oracle_wram_range" in case:
        cmd += ["--oracle-wram-range", str(case["oracle_wram_range"])]
    if "oracle_spc_port" in case:
        cmd += ["--oracle-spc-port", str(case["oracle_spc_port"])]
    if case.get("oracle_spc_after_ipl"):
        cmd.append("--oracle-spc-after-ipl")
    if "oracle_spc_pass_count" in case:
        cmd += ["--oracle-spc-pass-count", str(case["oracle_spc_pass_count"])]
    if case.get("stop_on_oracle"):
        cmd.append("--stop-on-oracle")
    if case.get("require_pass"):
        cmd.append("--require-pass")
    if "framebuffer_hash" in case:
        cmd += ["--expect-hash", str(case["framebuffer_hash"])]
    if "audio_hash" in case:
        cmd += ["--expect-audio-hash", str(case["audio_hash"])]
    watch_offsets = case.get("watch_wram_u16", [])
    if isinstance(watch_offsets, (int, str)):
        watch_offsets = [watch_offsets]
    for offset in watch_offsets:
        cmd += ["--watch-wram-u16", str(offset)]
    test_watch_offset = case.get("test_number_watch_offset")
    if test_watch_offset is not None and str(test_watch_offset) not in [str(x) for x in watch_offsets]:
        cmd += ["--watch-wram-u16", str(test_watch_offset)]
    cmd.append(str(rom))
    return cmd
def evaluate_result(base: pathlib.Path, case: dict, result: dict, proc_returncode: int, stderr: str) -> tuple[bool, str]:
    if proc_returncode != 0 or not result.get("loaded") or not result.get("expectation_met", True) or result.get("error"):
        return False, f"runner failed (exit {proc_returncode}): {json.dumps(result, sort_keys=True)}" + (f" stderr={stderr.strip()}" if stderr.strip() else "")
    expected_status = case.get("status")
    if expected_status is not None and result.get("status") != expected_status:
        return False, f"status {result.get('status')!r} != {expected_status!r}"
    expected_hardware = case.get("expected_hardware")
    if expected_hardware is not None and result.get("hardware") != expected_hardware:
        return False, f"hardware {result.get('hardware')!r} != {expected_hardware!r}"
    expected_support = case.get("expected_hardware_support")
    if expected_support is not None and result.get("hardware_support") != expected_support:
        return False, f"hardware support {result.get('hardware_support')!r} != {expected_support!r}"
    allowed_support = case.get("hardware_support_in")
    if allowed_support is not None:
        if not isinstance(allowed_support, list) or not allowed_support:
            return False, "hardware_support_in must be a non-empty array"
        if result.get("hardware_support") not in allowed_support:
            return False, f"hardware support {result.get('hardware_support')!r} not in {allowed_support!r}"
    if case.get("require_hardware_implemented") and not result.get("hardware_implemented", False):
        return False, "cartridge enhancement hardware is recognized but not implemented"
    test_watch_offset = case.get("test_number_watch_offset")
    if test_watch_offset is not None:
        diagnostic = find_wram_watch(result, int(str(test_watch_offset), 0))
        if diagnostic is None:
            return False, "requested test-number WRAM watch was not returned"
        result["diagnostic_test_number"] = int(diagnostic["value"])
        catalog_name = case.get("test_catalog")
        if catalog_name:
            catalog_path = resolved_path(base, str(catalog_name))
            if not catalog_path.is_file():
                return False, f"test catalog not found: {catalog_path}"
            catalog = parse_test_catalog(catalog_path)
            if not catalog:
                return False, f"no 'Test XXXX:' entries in catalog: {catalog_path}"
            num = result["diagnostic_test_number"]
            result["diagnostic_test"] = catalog.get(num)
            result["diagnostic_catalog_last_test"] = max(catalog)
            if case.get("require_catalog_complete") and num < max(catalog):
                return False, f"stopped at test {num:04x}/{max(catalog):04x}: {catalog.get(num, 'unknown test')}"
    return True, "pass"
def run_once(runner: pathlib.Path, base: pathlib.Path, case: dict, rom: pathlib.Path, timeout: float) -> tuple[bool, str, dict]:
    cmd = build_command(runner, case, rom)
    try:
        proc = subprocess.run(cmd, text=True, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return False, f"timeout after {timeout:g}s", {"passed": False, "error": "timeout", "timeout_seconds": timeout}
    lines = [line for line in proc.stdout.splitlines() if line.strip()]
    if not lines:
        return False, f"runner produced no JSON (exit {proc.returncode}): {proc.stderr.strip()}", {"passed": False, "error": "no-json", "exit_code": proc.returncode}
    try:
        result = json.loads(lines[-1])
    except json.JSONDecodeError as exc:
        return False, f"invalid runner JSON: {exc}: {lines[-1][:200]}", {"passed": False, "error": "invalid-json", "exit_code": proc.returncode}
    ok, reason = evaluate_result(base, case, result, proc.returncode, proc.stderr)
    if not ok:
        return False, reason, {"passed": False, "error": "expectation-failed", "result": result}
    result["passed"] = True
    return True, "pass", result
def observation_tuple(result: dict):
    return (
        result.get("framebuffer_hash"), result.get("audio_hash"), result.get("master_clocks"),
        result.get("cpu_cycles"), result.get("diagnostic_test_number"), result.get("status"),
        result.get("hardware"), result.get("hardware_support")
    )
def merge_checkpoint(parent: dict, checkpoint: dict) -> dict:
    merged = {k: copy.deepcopy(v) for k, v in parent.items() if k not in ("checkpoints", "name", "optional")}
    merged.update(copy.deepcopy(checkpoint))
    return merged
def run_logical_case(runner: pathlib.Path, base: pathlib.Path, case: dict, default_timeout: float) -> tuple[str, str, dict]:
    name = str(case.get("name") or case.get("rom") or "unnamed")
    rom = resolved_path(base, str(case["rom"]))
    if not rom.is_file():
        if case.get("optional"):
            return "skip", f"{name}: optional ROM not found: {rom}", {"name": name, "rom": str(rom), "skipped": True, "reason": "rom-not-found"}
        return "fail", f"{name}: ROM not found: {rom}", {"name": name, "rom": str(rom), "passed": False, "error": "rom-not-found"}
    expected_rom_hash = case.get("rom_sha256")
    actual_rom_hash = sha256_file(rom)
    if expected_rom_hash is not None and actual_rom_hash.lower() != str(expected_rom_hash).lower():
        return "fail", f"{name}: ROM SHA-256 mismatch: {actual_rom_hash} != {expected_rom_hash}", {
            "name": name, "rom": str(rom), "passed": False, "error": "rom-sha256-mismatch",
            "expected_sha256": str(expected_rom_hash), "actual_sha256": actual_rom_hash}
    catalog_name = case.get("test_catalog")
    if catalog_name and case.get("test_catalog_sha256"):
        catalog_path = resolved_path(base, str(catalog_name))
        if not catalog_path.is_file():
            return "fail", f"{name}: test catalog not found: {catalog_path}", {"name": name, "rom": str(rom), "passed": False, "error": "catalog-not-found"}
        actual_catalog_hash = sha256_file(catalog_path)
        expected_catalog_hash = str(case["test_catalog_sha256"])
        if actual_catalog_hash.lower() != expected_catalog_hash.lower():
            return "fail", f"{name}: catalog SHA-256 mismatch: {actual_catalog_hash} != {expected_catalog_hash}", {
                "name": name, "rom": str(rom), "passed": False, "error": "catalog-sha256-mismatch",
                "expected_sha256": expected_catalog_hash, "actual_sha256": actual_catalog_hash}
    checkpoints = case.get("checkpoints")
    if checkpoints is not None:
        if not isinstance(checkpoints, list) or not checkpoints:
            return "fail", f"{name}: checkpoints must be a non-empty array", {"name": name, "rom": str(rom), "passed": False, "error": "invalid-checkpoints"}
        cp_reports = []
        for index, checkpoint in enumerate(checkpoints):
            if not isinstance(checkpoint, dict):
                return "fail", f"{name}: checkpoint {index} is not an object", {"name": name, "rom": str(rom), "passed": False, "error": "invalid-checkpoint", "checkpoint": index}
            effective = merge_checkpoint(case, checkpoint)
            timeout = float(effective.get("timeout_seconds", case.get("timeout_seconds", default_timeout)))
            ok, reason, result = run_once(runner, base, effective, rom, timeout)
            cp_label = checkpoint.get("name") or (f"cycle {checkpoint['cycles']}" if "cycles" in checkpoint else f"frame {checkpoint.get('frames', 60)}")
            cp_report = {"index": index, "label": cp_label, "passed": ok, "result": result}
            cp_reports.append(cp_report)
            if not ok:
                report = {"name": name, "rom": str(rom), "passed": False, "error": "checkpoint-failed", "first_failing_checkpoint": index, "first_failing_label": cp_label, "checkpoints": cp_reports}
                return "fail", f"{name}: first divergence at {cp_label}: {reason}", report
        return "pass", f"{name}: PASS {len(cp_reports)} checkpoints", {"name": name, "rom": str(rom), "passed": True, "checkpoints": cp_reports}
    runs = int(case.get("determinism_runs", 1))
    if runs < 1:
        return "fail", f"{name}: determinism_runs must be >= 1", {"name": name, "rom": str(rom), "passed": False, "error": "invalid-determinism-runs"}
    timeout = float(case.get("timeout_seconds", default_timeout))
    run_results = []
    for index in range(runs):
        ok, reason, result = run_once(runner, base, case, rom, timeout)
        if not ok:
            return "fail", f"{name}: {reason} (run {index + 1}/{runs})", {"name": name, "rom": str(rom), "passed": False, "error": "run-failed", "run": index + 1, "result": result}
        run_results.append(result)
    observed = [observation_tuple(r) for r in run_results]
    if any(value != observed[0] for value in observed[1:]):
        return "fail", f"{name}: nondeterministic results: {observed}", {"name": name, "rom": str(rom), "passed": False, "error": "nondeterministic", "runs": run_results}
    first = run_results[0]
    suffix = f" test={first['diagnostic_test_number']:04x}" if first.get("diagnostic_test_number") is not None else ""
    return "pass", f"{name}: PASS framebuffer={first.get('framebuffer_hash')} audio={first.get('audio_hash')}{suffix}", {
        "name": name, "rom": str(rom), "passed": True, "determinism_runs": runs,
        "framebuffer_hash": first.get("framebuffer_hash"), "audio_hash": first.get("audio_hash"),
        "master_clocks": first.get("master_clocks"), "cpu_cycles": first.get("cpu_cycles"),
        "diagnostic_test_number": first.get("diagnostic_test_number"), "status": first.get("status"),
        "hardware": first.get("hardware"), "hardware_support": first.get("hardware_support")
    }
def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--runner", required=True, type=pathlib.Path)
    ap.add_argument("--manifest", required=True, type=pathlib.Path)
    ap.add_argument("--timeout", type=float, default=30.0, help="default per-run timeout in seconds")
    ap.add_argument("--report-json", type=pathlib.Path, help="write machine-readable per-case results")
    ap.add_argument("--fail-fast", action="store_true", help="stop after the first failing logical case")
    args = ap.parse_args()
    manifest_path = args.manifest.resolve()
    data = json.loads(manifest_path.read_text(encoding="utf-8"))
    cases = data.get("cases") if isinstance(data, dict) else data
    if not isinstance(cases, list) or not cases:
        raise SystemExit("manifest must contain a non-empty 'cases' array")
    failures = 0
    skipped = 0
    report_cases = []
    for case in cases:
        if not isinstance(case, dict) or "rom" not in case:
            print("FAIL invalid manifest case: each case needs a rom", file=sys.stderr)
            failures += 1
            if args.fail_fast:
                break
            continue
        status, message, report = run_logical_case(args.runner.resolve(), manifest_path.parent, case, args.timeout)
        print(status.upper() + " " + message)
        report_cases.append(report)
        failures += int(status == "fail")
        skipped += int(status == "skip")
        if status == "fail" and args.fail_fast:
            break
    executed = len(report_cases) - skipped
    passed = executed - failures
    print(f"External ROM regression summary: {passed}/{executed} executed passed, {skipped} skipped")
    if args.report_json:
        args.report_json.parent.mkdir(parents=True, exist_ok=True)
        payload = {
            "format": 2,
            "manifest": str(manifest_path),
            "cases_total": len(cases),
            "executed": executed,
            "passed": passed,
            "failed": failures,
            "skipped": skipped,
            "cases": report_cases,
        }
        args.report_json.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0 if failures == 0 else 1
if __name__ == "__main__":
    raise SystemExit(main())
