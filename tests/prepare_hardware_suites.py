import argparse
import hashlib
import json
import pathlib
def find_one(root: pathlib.Path, names: list[str]):
    lower = {n.lower() for n in names}
    for p in root.rglob("*"):
        if p.is_file() and p.name.lower() in lower:
            return p
    return None
def sha256(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()
def rel(path: pathlib.Path, base: pathlib.Path) -> str:
    try:
        return str(path.resolve().relative_to(base.resolve()))
    except ValueError:
        return str(path.resolve())
def add_gilyon(root: pathlib.Path, out: pathlib.Path, cases: list[dict]):
    cpu = find_one(root, ["cputest-full.sfc"])
    cpu_catalog = find_one(root, ["tests-full.txt"])
    if cpu and cpu_catalog:
        cases.append({
            "name": "gilyon 65C816 full conformance",
            "suite": "gilyon/snes-tests",
            "license": "MIT",
            "rom": rel(cpu, out.parent),
            "rom_sha256": sha256(cpu),
            "frames": 1800,
            "determinism_runs": 2,
            "test_number_watch_offset": "0x10",
            "test_catalog": rel(cpu_catalog, out.parent),
            "test_catalog_sha256": sha256(cpu_catalog),
            "require_catalog_complete": True,
            "timeout_seconds": 90,
        })
    spc = find_one(root, ["spctest.sfc"])
    spc_catalog = None
    if spc:
        for p in spc.parent.glob("tests*.txt"):
            if p.name.lower() == "tests.txt":
                spc_catalog = p
                break
    if spc and spc_catalog:
        cases.append({
            "name": "gilyon SPC700 conformance",
            "suite": "gilyon/snes-tests",
            "license": "MIT",
            "rom": rel(spc, out.parent),
            "rom_sha256": sha256(spc),
            "frames": 1800,
            "oracle_spc_port": "0:1:2",
            "oracle_spc_after_ipl": True,
            "stop_on_oracle": True,
            "require_pass": True,
            "status": "pass",
            "test_number_watch_offset": "0x4",
            "test_catalog": rel(spc_catalog, out.parent),
            "test_catalog_sha256": sha256(spc_catalog),
            "timeout_seconds": 90,
        })
def add_undisbeliever(root: pathlib.Path, out: pathlib.Path, cases: list[dict], limit: int):
    candidates = [p for p in root.rglob("*.sfc") if "undisbeliever" in str(p).lower() or "snes-test-roms" in str(p).lower()]
    for p in sorted(candidates)[:limit if limit > 0 else None]:
        cases.append({
            "name": f"undisbeliever determinism: {p.stem}",
            "suite": "undisbeliever/snes-test-roms",
            "license": "Zlib",
            "rom": rel(p, out.parent),
            "rom_sha256": sha256(p),
            "frames": 180,
            "determinism_runs": 2,
            "optional": True,
            "timeout_seconds": 30,
            "note": "Determinism smoke only until a hardware/reference golden is supplied; do not treat self-output as a correctness oracle.",
        })
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True, type=pathlib.Path, help="directory containing external test-suite files")
    ap.add_argument("--output", required=True, type=pathlib.Path)
    ap.add_argument("--undisbeliever-limit", type=int, default=64)
    args = ap.parse_args()
    root = args.root.resolve()
    out = args.output.resolve()
    cases = []
    add_gilyon(root, out, cases)
    add_undisbeliever(root, out, cases, args.undisbeliever_limit)
    if not cases:
        raise SystemExit(f"no recognized SNES hardware-test assets found below {root}")
    out.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "format": 2,
        "generated_by": "tests/prepare_hardware_suites.py",
        "provenance": {
            "gilyon/snes-tests": {"license": "MIT", "url": "https://github.com/gilyon/snes-tests"},
            "undisbeliever/snes-test-roms": {"license": "Zlib", "url": "https://github.com/undisbeliever/snes-test-roms"},
        },
        "cases": cases,
    }
    out.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {len(cases)} cases to {out}")
if __name__ == "__main__":
    main()
