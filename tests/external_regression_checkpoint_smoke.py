import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile
def main() -> int:
    harness = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as td:
        root = pathlib.Path(td)
        rom = root / "synthetic.sfc"
        rom.write_bytes(b"synthetic")
        rom_hash = hashlib.sha256(rom.read_bytes()).hexdigest()
        runner = root / "runner.py"
        runner.write_text(r'''#!/usr/bin/env python3
import json,sys
frames=60
for i,a in enumerate(sys.argv):
    if a == "--frames": frames=int(sys.argv[i+1])
expected=None
for i,a in enumerate(sys.argv):
    if a == "--expect-hash": expected=sys.argv[i+1].lower().replace("0x","")
h={60:"000000000000003c",120:"0000000000000078",180:"00000000000000b4"}.get(frames,"0000000000000000")
result={"loaded":True,"expectation_met": expected is None or expected == h,"error":"","status":"unclassified","framebuffer_hash":h,"audio_hash":"0000000000000001","master_clocks":frames*100,"cpu_cycles":frames*10,"wram_u16_watches":[]}
print(json.dumps(result))
sys.exit(0 if result["expectation_met"] else 1)
''', encoding="utf-8")
        runner.chmod(0o755)
        manifest = root / "manifest.json"
        report = root / "report.json"
        manifest.write_text(json.dumps({"cases":[
            {"name":"checkpoints","rom":"synthetic.sfc","rom_sha256":rom_hash,"checkpoints":[
                {"frames":60,"framebuffer_hash":"000000000000003c"},
                {"frames":120,"framebuffer_hash":"0000000000000078"},
                {"frames":180,"framebuffer_hash":"00000000000000b4"}]},
            {"name":"missing optional","rom":"not-present.sfc","optional":True}
        ]}), encoding="utf-8")
        proc = subprocess.run([sys.executable,str(harness),"--runner",str(runner),"--manifest",str(manifest),"--report-json",str(report)], text=True, capture_output=True)
        if proc.returncode != 0:
            print(proc.stdout); print(proc.stderr, file=sys.stderr); return 1
        data=json.loads(report.read_text())
        assert data["format"] == 2
        assert data["passed"] == 1 and data["skipped"] == 1 and data["failed"] == 0
        assert len(data["cases"][0]["checkpoints"]) == 3
        wrong_hash = root / "wrong-hash.json"
        wrong_hash.write_text(json.dumps({"cases":[{"name":"pinned asset","rom":"synthetic.sfc","rom_sha256":"00"*32,"frames":60}]}), encoding="utf-8")
        proc = subprocess.run([sys.executable,str(harness),"--runner",str(runner),"--manifest",str(wrong_hash)], text=True, capture_output=True)
        assert proc.returncode != 0 and "SHA-256 mismatch" in proc.stdout
        bad = root / "bad.json"
        bad.write_text(json.dumps({"cases":[{"name":"first divergence","rom":"synthetic.sfc","checkpoints":[
            {"frames":60,"framebuffer_hash":"000000000000003c"},
            {"frames":120,"framebuffer_hash":"deadbeefdeadbeef"},
            {"frames":180,"framebuffer_hash":"00000000000000b4"}]}]}), encoding="utf-8")
        bad_report=root/"bad-report.json"
        proc=subprocess.run([sys.executable,str(harness),"--runner",str(runner),"--manifest",str(bad),"--report-json",str(bad_report)], text=True, capture_output=True)
        assert proc.returncode != 0
        data=json.loads(bad_report.read_text())
        assert data["cases"][0]["first_failing_checkpoint"] == 1
        assert len(data["cases"][0]["checkpoints"]) == 2
    print("external checkpoint harness: PASS")
    return 0
if __name__ == "__main__":
    raise SystemExit(main())
