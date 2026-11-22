import argparse
import pathlib
import subprocess
import sys
import tempfile
def main() -> int:
    ap=argparse.ArgumentParser()
    ap.add_argument("--root", required=True, type=pathlib.Path)
    ap.add_argument("--runner", required=True, type=pathlib.Path)
    ap.add_argument("--report-json", type=pathlib.Path)
    ap.add_argument("--timeout", type=float, default=60.0)
    args=ap.parse_args()
    here=pathlib.Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix="snesultimate-hw-") as td:
        manifest=pathlib.Path(td)/"hardware-suites.json"
        prep=subprocess.run([sys.executable,str(here/"prepare_hardware_suites.py"),"--root",str(args.root),"--output",str(manifest)])
        if prep.returncode != 0:
            return prep.returncode
        cmd=[sys.executable,str(here/"external_regression.py"),"--runner",str(args.runner),"--manifest",str(manifest),"--timeout",str(args.timeout)]
        if args.report_json:
            cmd += ["--report-json",str(args.report_json)]
        return subprocess.run(cmd).returncode
if __name__ == "__main__":
    raise SystemExit(main())
