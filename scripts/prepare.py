# SPDX-License-Identifier: BSD-3-Clause
"""Fetch pinned ReShade, apply integration, and install canonical native sources."""
from pathlib import Path
import argparse
import json
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PIN = json.loads((ROOT / "upstream.json").read_text(encoding="utf-8"))

def git(*args, cwd=None, capture=False, check=True):
    return subprocess.run(["git", *map(str, args)], cwd=cwd, check=check,
                          text=True, stdout=subprocess.PIPE if capture else None,
                          stderr=subprocess.PIPE if capture else None)

def previous_patches():
    """Earlier committed versions of the integration patch, newest first."""
    log = git("log", "--format=%H", "--", PIN["patch"], cwd=ROOT, capture=True, check=False)
    seen = set()
    for commit in log.stdout.split() if log.returncode == 0 else []:
        blob = subprocess.run(["git", "show", f"{commit}:{PIN['patch']}"], cwd=ROOT, capture_output=True)
        if blob.returncode == 0 and blob.stdout not in seen:
            seen.add(blob.stdout)
            yield commit, blob.stdout

def apply_integration(target, patch):
    """Apply the integration patch, or replace a fully applied earlier version of it."""
    def check(path, *flags):
        return git("apply", "--ignore-space-change", *flags, "--check", path, cwd=target, check=False, capture=True).returncode == 0
    if check(patch):
        git("apply", "--ignore-space-change", patch, cwd=target)
        return "applied"
    if check(patch, "--reverse"):
        return "already applied"
    current = patch.read_bytes()
    for commit, content in previous_patches():
        if content == current:
            continue
        with tempfile.NamedTemporaryFile(suffix=".patch", delete=False) as handle:
            handle.write(content)
        old = Path(handle.name)
        try:
            if check(old, "--reverse"):
                git("apply", "--ignore-space-change", "--reverse", old, cwd=target)
                if not check(patch):
                    git("apply", "--ignore-space-change", old, cwd=target)
                    break
                git("apply", "--ignore-space-change", patch, cwd=target)
                return f"replaced patch from {commit[:7]}"
        finally:
            old.unlink()
    raise SystemExit("Integration patch conflicts with local changes; nothing was reset. "
                     "If external/reshade holds other edits, delete it and run prepare.py again.")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, help="Optional existing upstream checkout to avoid downloading cached objects.")
    args = parser.parse_args()
    target = ROOT / "external" / "reshade"
    if not target.exists():
        target.parent.mkdir(parents=True, exist_ok=True)
        origin = str(args.cache.resolve()) if args.cache else PIN["repository"]
        git("clone", "--no-hardlinks", "--no-checkout", origin, target)
        git("config", "core.autocrlf", "false", cwd=target)
        git("checkout", "--detach", PIN["commit"], cwd=target)
        git("remote", "set-url", "origin", PIN["repository"], cwd=target)
    head = git("rev-parse", "HEAD", cwd=target, capture=True).stdout.strip()
    if head != PIN["commit"]:
        raise SystemExit("ReShade checkout is not at the pinned commit. No reset was performed.")
    git("submodule", "init", cwd=target)
    if args.cache:
        records = git("config", "-f", ".gitmodules", "--get-regexp",
                      r"^submodule\..*\.path$", cwd=target, capture=True).stdout.splitlines()
        for record in records:
            key, relative = record.split(None, 1)
            cached = args.cache.resolve() / relative
            if cached.exists():
                git("config", key.removesuffix(".path") + ".url", cached, cwd=target)
    command = ["-c", "protocol.file.allow=always"] if args.cache else []
    git(*command, "submodule", "update", "--init", "--recursive", "--jobs", "4", cwd=target)
    git("submodule", "sync", "--recursive", cwd=target)
    print("Integration patch:", apply_integration(target, ROOT / PIN["patch"]))
    for name in ("er_batch.cpp", "er_batch.hpp", "er_scratch_policy.hpp"):
        shutil.copyfile(ROOT / "src" / "native" / name, target / "source" / "d3d12" / name)
    registry = target / "deps" / "glad" / "glad" / "files"
    if not registry.is_dir():
        raise SystemExit("GLAD checkout is incomplete.")
    for path in (target / "deps" / "khronos").glob("*.xml"):
        shutil.copyfile(path, registry / path.name)
    git("diff", "--check", cwd=target)
    print("Pinned ReShade prepared; canonical native source and matching registries installed.")

if __name__ == "__main__":
    main()
