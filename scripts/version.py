# SPDX-License-Identifier: BSD-3-Clause
"""The version file is shared by packaging, tag validation and release publishing."""
from pathlib import Path
import argparse
import json
import re

ROOT = Path(__file__).resolve().parents[1]

def release_version():
    config = json.loads((ROOT / "version.json").read_text(encoding="utf-8"))
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?", config["version"]):
        raise ValueError("Invalid release version.")
    if type(config.get("prerelease")) is not bool:
        raise ValueError("prerelease must be a boolean.")
    return config

def check_tag(tag):
    expected = "v" + release_version()["version"]
    if tag != expected:
        raise ValueError(f"Tag {tag!r} does not match {expected!r} from version.json.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-tag")
    args = parser.parse_args()
    if args.check_tag:
        check_tag(args.check_tag)
    print(release_version()["version"])
