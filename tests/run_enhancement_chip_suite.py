import argparse
import pathlib
import subprocess
import sys
import tempfile
def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True, type=pathlib.Path)
    ap.add_argument("--runner", required=True, type=pathlib.Path)
    ap.add_argument("--goldens", type=pathlib.Path)
    ap.add_argument("--report-json", type=pathlib.Path)
    ap.add_argument("--timeout", type=float, default=90.0)
    args = ap.parse_args()
    here = pathlib.Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix="snesultimate-enhancement-") as td:
        manifest = pathlib.Path(td) / "enhancement-chip-roms.json"
        cmd = [sys.executable, str(here / "prepare_enhancement_chip_manifest.py"),
               "--root", str(args.root), "--output", str(manifest)]
        if args.goldens:
            cmd += ["--goldens", str(args.goldens)]
        proc = subprocess.run(cmd)
        if proc.returncode:
            return proc.returncode
        run = [sys.executable, str(here / "external_regression.py"),
               "--runner", str(args.runner), "--manifest", str(manifest), "--timeout", str(args.timeout)]
        if args.report_json:
            run += ["--report-json", str(args.report_json)]
        return subprocess.run(run).returncode
if __name__ == "__main__":
    raise SystemExit(main())
