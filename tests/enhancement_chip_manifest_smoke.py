import json
import pathlib
import subprocess
import sys
import tempfile
def main() -> int:
    generator = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as td:
        root = pathlib.Path(td)
        for name in ["Super Mario RPG (USA).sfc", "Star Ocean (USA).sfc", "Mega Man X2 (USA).sfc", "Top Gear 3000 (USA).sfc"]:
            (root / name).write_bytes((name + " synthetic manifest smoke").encode())
        out = root / "manifest.json"
        proc = subprocess.run([sys.executable, str(generator), "--root", str(root), "--output", str(out)], capture_output=True, text=True)
        assert proc.returncode == 0, proc.stderr
        data = json.loads(out.read_text())
        by_hw = {c["expected_hardware"]: c for c in data["cases"]}
        assert {"SA-1", "S-DD1", "CX4", "DSP-4"}.issubset(by_hw)
        assert all(len(c["rom_sha256"]) == 64 for c in data["cases"])
        assert by_hw["DSP-4"]["hardware_support_in"] == ["firmware LLE", "partial / HLE"]
        assert all(c["determinism_runs"] == 2 for c in data["cases"])
    print("enhancement-chip manifest generator: PASS")
    return 0
if __name__ == "__main__":
    raise SystemExit(main())
