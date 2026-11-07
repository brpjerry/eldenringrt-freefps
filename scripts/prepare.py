# SPDX-License-Identifier: BSD-3-Clause
"""Fetch pinned ReShade, apply integration, and install canonical native sources."""
from pathlib import Path
import argparse
import json
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PIN = json.loads((ROOT / "upstream.json").read_text(encoding="utf-8"))

def git(*args, cwd=None, capture=False, check=True):
    return subprocess.run(["git", *map(str, args)], cwd=cwd, check=check,
                          text=True, stdout=subprocess.PIPE if capture else None,
                          stderr=subprocess.PIPE if capture else None)

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
    patch = ROOT / PIN["patch"]
    forward = git("apply", "--ignore-space-change", "--check", patch, cwd=target, check=False, capture=True)
    if forward.returncode == 0:
        git("apply", "--ignore-space-change", patch, cwd=target)
    else:
        reverse = git("apply", "--ignore-space-change", "--reverse", "--check", patch, cwd=target, check=False, capture=True)
        if reverse.returncode:
            raise SystemExit("Integration patch conflicts with local changes; nothing was reset.")
    for name in ("er_batch.cpp", "er_batch.hpp"):
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
