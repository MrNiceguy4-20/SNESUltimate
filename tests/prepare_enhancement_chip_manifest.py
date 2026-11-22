import argparse
import hashlib
import json
import pathlib
import re
ROM_EXTS = {".sfc", ".smc", ".fig", ".swc", ".bin", ".rom"}
TARGETS = [
    ("SA-1", "SA-1", ["super mario rpg", "kirby super star", "kirbys dream land 3", "jikkyo oshaberi parodius"]),
    ("Super FX / GSU-1", "SuperFX / GSU-1", ["star fox", "stunt race fx", "vortex"]),
    ("Super FX 2 / GSU-2", "SuperFX 2 / GSU-2", ["yoshis island", "super mario world 2", "doom", "star fox 2"]),
    ("S-DD1", "S-DD1", ["star ocean", "street fighter alpha 2", "street fighter zero 2"]),
    ("SPC7110", "SPC7110", ["momotaro dentetsu happy", "super power league 4"]),
    ("SPC7110 + RTC", "SPC7110 + RTC", ["tengai makyou zero", "far east of eden zero"]),
    ("DSP-1", "DSP-1", ["pilotwings", "super mario kart"]),
    ("DSP-2", "DSP-2", ["dungeon master"]),
    ("DSP-3", "DSP-3", ["sd gundam gx"]),
    ("DSP-4", "DSP-4", ["top gear 3000"]),
    ("CX4", "CX4", ["mega man x2", "megaman x2", "mega man x3", "megaman x3", "rockman x2", "rockman x3"]),
    ("OBC-1", "OBC-1", ["metal combat falcons revenge", "metal combat"]),
    ("S-RTC", "S-RTC", ["dai kaijuu monogatari ii", "daikaijuu monogatari ii"]),
    ("ST010", "ST010", ["f1 roc ii", "f1 roc 2"]),
    ("ST011", "ST011", ["hayazashi nidan morita shogi"]),
    ("ST018", "ST018", ["hayazashi nidan morita shogi 2"]),
]
def norm(text: str) -> str:
    text = text.lower().replace("'", "")
    return " ".join(re.sub(r"[^a-z0-9]+", " ", text).split())
def sha256(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()
def rel(path: pathlib.Path, base: pathlib.Path) -> str:
    try:
        return str(path.resolve().relative_to(base.resolve()))
    except ValueError:
        return str(path.resolve())
def load_goldens(path: pathlib.Path | None) -> dict:
    if not path:
        return {}
    data = json.loads(path.read_text(encoding="utf-8"))
    entries = data.get("roms", data) if isinstance(data, dict) else {}
    if not isinstance(entries, dict):
        raise SystemExit("golden file must contain an object keyed by ROM SHA-256")
    return {str(k).lower(): v for k, v in entries.items() if isinstance(v, dict)}
def match_target(path: pathlib.Path):
    name = norm(path.stem)
    for label, hardware, fragments in reversed(TARGETS):
        for fragment in fragments:
            if norm(fragment) in name:
                return label, hardware
    return None
def default_case(label: str, hardware: str, rom: pathlib.Path, digest: str, output: pathlib.Path) -> dict:
    case = {
        "name": f"enhancement {label}: {rom.stem}",
        "suite": "private commercial enhancement-chip ROMs",
        "rom": rel(rom, output.parent),
        "rom_sha256": digest,
        "expected_hardware": hardware,
        "require_hardware_implemented": True,
        "frames": 600,
        "determinism_runs": 2,
        "timeout_seconds": 90,
        "tags": ["enhancement-chip", label],
        "note": "Without trusted golden hashes this is a chip-selection + determinism smoke, not a correctness oracle.",
    }
    if hardware.startswith("DSP-") or hardware in {"CX4", "ST010", "ST011", "ST018"}:
        case["hardware_support_in"] = ["firmware LLE", "partial / HLE"]
    return case
def apply_golden(case: dict, golden: dict) -> None:
    if "framebuffer_hash" in golden:
        case["framebuffer_hash"] = str(golden["framebuffer_hash"])
    if "audio_hash" in golden:
        case["audio_hash"] = str(golden["audio_hash"])
    if "frames" in golden:
        case["frames"] = int(golden["frames"])
    if "hardware_support" in golden:
        case["expected_hardware_support"] = str(golden["hardware_support"])
        case.pop("hardware_support_in", None)
    checkpoints = golden.get("checkpoints")
    if isinstance(checkpoints, list) and checkpoints:
        case["checkpoints"] = checkpoints
        case.pop("frames", None)
        case.pop("determinism_runs", None)
    case["oracle_source"] = golden.get("source", "trusted user-supplied golden")
    case["note"] = "Pinned real-ROM regression with externally supplied golden observations."
def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True, type=pathlib.Path, help="private directory containing legally obtained SNES ROMs")
    ap.add_argument("--output", required=True, type=pathlib.Path)
    ap.add_argument("--goldens", type=pathlib.Path, help="optional JSON keyed by ROM SHA-256 with trusted hashes/checkpoints")
    ap.add_argument("--strict", action="store_true", help="fail if no representative ROM is found for every chip family")
    args = ap.parse_args()
    root = args.root.resolve()
    output = args.output.resolve()
    goldens = load_goldens(args.goldens.resolve() if args.goldens else None)
    cases = []
    found_labels = set()
    seen_paths = set()
    for rom in sorted(p for p in root.rglob("*") if p.is_file() and p.suffix.lower() in ROM_EXTS):
        matched = match_target(rom)
        if not matched or rom.resolve() in seen_paths:
            continue
        label, hardware = matched
        digest = sha256(rom)
        case = default_case(label, hardware, rom, digest, output)
        if digest.lower() in goldens:
            apply_golden(case, goldens[digest.lower()])
        cases.append(case)
        found_labels.add(label)
        seen_paths.add(rom.resolve())
    if not cases:
        raise SystemExit(f"no recognized enhancement-chip ROMs found below {root}")
    if args.strict:
        missing = sorted({x[0] for x in TARGETS} - found_labels)
        if missing:
            raise SystemExit("missing representative ROMs for: " + ", ".join(missing))
    output.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "format": 2,
        "generated_by": "tests/prepare_enhancement_chip_manifest.py",
        "rom_policy": "ROMs remain external; only paths and SHA-256 hashes are recorded.",
        "coverage": sorted(found_labels),
        "cases": cases,
    }
    output.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {len(cases)} enhancement-chip cases covering {len(found_labels)} chip families to {output}")
    return 0
if __name__ == "__main__":
    raise SystemExit(main())
