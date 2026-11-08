# SPDX-License-Identifier: BSD-3-Clause
"""Check the Git-visible publication set, excluding ignored dependencies and outputs."""
from pathlib import Path
import json
import re
import subprocess
import tomllib
from package_layout import validate_profile

ROOT = Path(__file__).resolve().parents[1]

def main():
    files = subprocess.check_output(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"], cwd=ROOT
    ).decode().split("\0")
    files = sorted(set(f for f in files if f))
    if not files:
        raise SystemExit("No Git-visible source files found.")
    forbidden = {".sl2", ".dxbc", ".dxil", ".dll", ".exe", ".asi", ".addon64", ".obj", ".pdb",
                 ".zip", ".etl", ".pix3", ".nsys-rep", ".log"}
    errors = []
    for name in files:
        path = ROOT / name
        if path.suffix.lower() in forbidden:
            errors.append(f"Unwanted generated/private file: {name}")
        if not path.is_file():
            errors.append(f"Missing or non-file source entry: {name}")
            continue
        if path.stat().st_size > 2_000_000:
            errors.append(f"Unexpectedly large source file: {name}")
        data = path.read_bytes()
        text = data.decode("utf-8", errors="replace")
        patterns = (
            r"(?i)[a-z]:[\\/]+users[\\/]+",
            r"(?<!\d)7656\d{13}(?!\d)",
            r"gh[pousr]_[A-Za-z0-9]{30,}",
            r"-----BEGIN (?:RSA |OPENSSH |EC )?PRIVATE KEY-----",
        )
        if any(re.search(p, text) for p in patterns):
            errors.append(f"Possible private path/account/credential: {name}")
        if b"\0" in data:
            errors.append(f"Binary content in publication source: {name}")
    for path in (ROOT / "package").rglob("*"):
        if path.is_file() and path.suffix.lower() in {".cmd", ".ps1", ".bat"}:
            errors.append(f"Player package contains a script: {path.name}")
    profile = tomllib.loads((ROOT / "package" / "eldenring-rt-batching.me3").read_text(encoding="utf-8"))
    try:
        validate_profile(profile, ROOT)
    except ValueError as error:
        errors.append(str(error))
    pin = json.loads((ROOT / "upstream.json").read_text(encoding="utf-8"))
    patch = (ROOT / pin["patch"]).read_text(encoding="utf-8")
    if "new file mode" in patch:
        errors.append("Integration patch duplicates canonical native source.")
    for path in (ROOT / "scripts").glob("*.py"):
        compile(path.read_text(encoding="utf-8"), str(path), "exec")
    if errors:
        raise SystemExit("\n".join(errors))
    print(f"PASS: {len(files)} Git-visible text files; profile, source layout and publication scan clean.")

if __name__ == "__main__":
    main()
