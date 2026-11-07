# SPDX-License-Identifier: BSD-3-Clause
"""Package only the runtime, configuration and notices; never ship developer scripts."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import tomllib
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PIN = json.loads((ROOT / "upstream.json").read_text(encoding="utf-8"))
BINARIES = ("ReShade64.asi", "RT-Pass-Probe.addon64")
TEMPLATES = ("README.txt", "ReShade.ini", "RTVegetation.ini", "eldenring-rt-batching.me3")

def sha(data):
    return hashlib.sha256(data).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binaries", type=Path, default=ROOT / "build" / "bin")
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    args = parser.parse_args()
    members = {name: (args.binaries / name).read_bytes() for name in BINARIES}
    hashes = {name: sha(members[name]) for name in BINARIES}
    for name, data in members.items():
        if not data.startswith(b"MZ"):
            raise SystemExit(f"{name} is not a Windows PE binary.")
    members.update({name: (ROOT / "package" / name).read_bytes() for name in TEMPLATES})
    members["LICENSE"] = (ROOT / "LICENSE").read_bytes()
    members["THIRD_PARTY.md"] = (ROOT / "THIRD_PARTY.md").read_bytes()
    for path in (ROOT / "licenses").rglob("*"):
        if path.is_file():
            members[path.relative_to(ROOT).as_posix()] = path.read_bytes()
    profile = tomllib.loads(members["eldenring-rt-batching.me3"].decode())
    if profile.get("start_online") is not False or profile.get("packages"):
        raise SystemExit("Public profile must be offline and contain no other packages.")
    if profile["natives"] != [{"path": "ReShade64.asi", "load_early": True}]:
        raise SystemExit("Unexpected public native list.")
    original = hashes == PIN["original_game_tested_sha256"]
    manifest = {
        "name": "Elden Ring RT Batching", "version": "1.0-portable-experimental",
        "runtime_base": PIN["tag"], "runtime_base_commit": PIN["commit"],
        "binary_sha256": hashes, "matches_original_game_tested_binaries": original,
        "launch_entry": "eldenring-rt-batching.me3", "includes_control_scripts": False,
        "default": {"batch_mode": 2, "update_interval": 1},
        "savefile": profile["savefile"],
        "validation_note": "Original in-game results apply only to the recorded original hashes.",
    }
    members["manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    for name, data in members.items():
        if Path(name).suffix.lower() in {".cmd", ".ps1", ".bat", ".sl2", ".dxbc", ".pdb", ".obj", ".log"}:
            raise SystemExit(f"Disallowed package file: {name}")
        for text in (data.decode("utf-8", errors="ignore"),
                     data.decode("utf-16-le", errors="ignore")):
            if re.search(r"(?i)[a-z]:[\\/]+users[\\/]+", text):
                raise SystemExit(f"Personal build path embedded in {name}. Use a release build without debug paths.")
        if re.search(rb"(?<![0-9])7656[0-9]{13}(?![0-9])", data):
            raise SystemExit(f"Possible account ID in {name}")
    members["SHA256SUMS.txt"] = "".join(
        f"{sha(data)}  {name}\n" for name, data in sorted(members.items())).encode()
    args.output.mkdir(parents=True, exist_ok=True)
    archive = args.output / "EldenRing-RT-Batching-1.0-portable.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, data in sorted(members.items()):
            entry = zipfile.ZipInfo("RT-Batching/" + name, (2026, 11, 7, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            z.writestr(entry, data)
    with zipfile.ZipFile(archive) as z:
        if z.testzip() is not None:
            raise SystemExit("ZIP integrity check failed.")
        for name, data in members.items():
            if z.read("RT-Batching/" + name) != data:
                raise SystemExit(f"ZIP member mismatch: {name}")
    archive.with_suffix(".zip.sha256").write_text(
        f"{sha(archive.read_bytes())}  {archive.name}\n", encoding="ascii")
    print(f"Packaged {len(members)} files. Original game-tested hashes: {original}.")
    print(archive)

if __name__ == "__main__":
    main()
