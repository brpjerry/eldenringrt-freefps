# SPDX-License-Identifier: BSD-3-Clause
"""Public package layout and ME3 Manager companion-file contract."""
BINARIES = ("EldenRingRT.dll", "RT-Pass-Probe.addon64")
TEMPLATES = ("README.txt", "ReShade.ini", "RTVegetation.ini", "eldenring-rt-batching.me3")

def companion_files(root):
    files = set(BINARIES[1:] + TEMPLATES + ("LICENSE", "THIRD_PARTY.md", "manifest.json", "SHA256SUMS.txt"))
    files.update(p.relative_to(root).as_posix() for p in (root / "licenses").rglob("*") if p.is_file())
    return sorted(files)

def public_native(root):
    return {"path": BINARIES[0], "load_early": True, "config": companion_files(root)}

def validate_profile(profile, root):
    if profile.get("start_online") is not False or profile.get("packages"):
        raise ValueError("Public profile must be standalone and offline.")
    if "savefile" in profile:
        raise ValueError("Public profile must not override the game save filename.")
    if profile.get("natives") != [public_native(root)]:
        raise ValueError("Public profile must load the runtime DLL early and list every companion file.")
