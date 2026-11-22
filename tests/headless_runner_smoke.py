import json
import pathlib
import subprocess
import sys
import tempfile
if len(sys.argv) != 3:
    raise SystemExit("usage: headless_runner_smoke.py RUNNER ROM")
runner, rom = sys.argv[1:]
with tempfile.TemporaryDirectory() as td:
    td = pathlib.Path(td)
    frame = td / "frame.ppm"
    wram = td / "wram.bin"
    trace = td / "trace.csv"
    audio = td / "audio.pcm"
    cmd = [runner, "--frames", "2", "--json", "--dump-frame", str(frame),
           "--dump-wram", str(wram), "--dump-audio", str(audio),
           "--trace", str(trace), "--trace-limit", "128",
           "--watch-wram-u16", "0x10", rom]
    first = subprocess.run(cmd, check=True, text=True, capture_output=True)
    result1 = json.loads(first.stdout.strip().splitlines()[-1])
    second = subprocess.run([runner, "--frames", "2", "--json", rom], check=True, text=True, capture_output=True)
    result2 = json.loads(second.stdout.strip().splitlines()[-1])
    assert result1["loaded"] and result2["loaded"]
    assert result1["wram_u16_watches"] == [{"offset": 0x10, "value": 0, "changes": 0}]
    assert result1["framebuffer_hash"] == result2["framebuffer_hash"]
    assert result1["audio_hash"] == result2["audio_hash"]
    assert result1["audio_frames"] == result2["audio_frames"]
    assert frame.read_bytes().startswith(b"P6\n")
    assert wram.stat().st_size == 128 * 1024
    assert audio.stat().st_size == result1["audio_frames"] * 4
    lines = trace.read_text().splitlines()
    assert lines[0].startswith("index,kind,start_master_clock")
    assert 1 < len(lines) <= 129
    print("PASS headless runner smoke: framebuffer=" + result1["framebuffer_hash"] +
          " audio=" + result1["audio_hash"])
