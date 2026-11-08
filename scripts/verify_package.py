# SPDX-License-Identifier: BSD-3-Clause
"""Verify the complete portable artifact before upload or publication."""
from pathlib import Path
import argparse
import configparser
import hashlib
import json
import re
import struct
import tomllib
import zipfile
from version import ROOT, release_version
from package_layout import BINARIES, TEMPLATES, validate_profile


def sha(data):
    return hashlib.sha256(data).hexdigest()

def validate_pe(data, name):
    if len(data) < 64 or data[:2] != b"MZ":
        raise ValueError(f"{name} has no DOS header.")
    offset = struct.unpack_from("<I", data, 60)[0]
    if offset + 26 > len(data) or data[offset:offset + 4] != b"PE\0\0":
        raise ValueError(f"{name} has no valid PE header.")
    machine = struct.unpack_from("<H", data, offset + 4)[0]
    characteristics = struct.unpack_from("<H", data, offset + 22)[0]
    magic = struct.unpack_from("<H", data, offset + 24)[0]
    if machine != 0x8664 or magic != 0x20B or not characteristics & 0x2000:
        raise ValueError(f"{name} must be an x64 PE32+ DLL.")

def validate_package(archive, expected_commit=None):
    version = release_version()["version"]
    expected_name = f"EldenRing-RT-Batching-{version}-portable.zip"
    if archive.name != expected_name:
        raise ValueError("Archive filename and version disagree.")
    sidecar = archive.with_suffix(".zip.sha256").read_text(encoding="ascii").strip()
    if sidecar != f"{sha(archive.read_bytes())}  {archive.name}":
        raise ValueError("Archive checksum mismatch.")
    expected = set(BINARIES + TEMPLATES + ("LICENSE", "THIRD_PARTY.md", "manifest.json", "SHA256SUMS.txt"))
    expected.update(p.relative_to(ROOT).as_posix() for p in (ROOT / "licenses").rglob("*") if p.is_file())
    with zipfile.ZipFile(archive) as z:
        names = z.namelist()
        if len(set(names)) != len(names) or set(names) != {"RT-Batching/" + n for n in expected}:
            raise ValueError("ZIP contains missing, duplicate or unexpected files.")
        if z.testzip() is not None:
            raise ValueError("ZIP CRC failure.")
        members = {n: z.read("RT-Batching/" + n) for n in expected}
    sums = {}
    for line in members["SHA256SUMS.txt"].decode("ascii").splitlines():
        match = re.fullmatch(r"([a-f0-9]{64})  (.+)", line)
        if not match or match[2] in sums:
            raise ValueError("Malformed or duplicate member checksum.")
        sums[match[2]] = match[1]
    if set(sums) != expected - {"SHA256SUMS.txt"}:
        raise ValueError("Checksum inventory is incomplete.")
    for name, data in members.items():
        if name in sums and sha(data) != sums[name]:
            raise ValueError(f"Member checksum mismatch: {name}")
        for text in (data.decode("utf-8", errors="ignore"), data.decode("utf-16-le", errors="ignore")):
            if re.search(r"(?i)[a-z]:[\\/]+users[\\/]+", text):
                raise ValueError(f"Personal path in package: {name}")
        if re.search(rb"(?<![0-9])7656[0-9]{13}(?![0-9])", data):
            raise ValueError(f"Possible account ID: {name}")
    manifest = json.loads(members["manifest.json"])
    if manifest["version"] != version:
        raise ValueError("Manifest version mismatch.")
    if expected_commit and manifest["source_commit"] != expected_commit:
        raise ValueError("Artifact was built from a different commit.")
    if not re.fullmatch("[a-f0-9]{40}", manifest["source_commit"]):
        raise ValueError("Missing source commit.")
    for name in BINARIES:
        validate_pe(members[name], name)
        if sha(members[name]) != manifest["binary_sha256"][name]:
            raise ValueError(f"Manifest binary hash mismatch: {name}")
    profile = tomllib.loads(members["eldenring-rt-batching.me3"].decode())
    validate_profile(profile, ROOT)
    if manifest.get("savefile") is not None:
        raise ValueError("Public package must not override the game save filename.")
    config = configparser.ConfigParser()
    config.read_string(members["RTVegetation.ini"].decode())
    if config.getint("Settings", "UpdateInterval") != 1 or config.getint("Settings", "BatchMode") != 2:
        raise ValueError("Unexpected default batching settings.")
    return manifest

def find_archive():
    archives = list((ROOT / "dist").glob("*.zip"))
    if len(archives) != 1:
        raise ValueError("Expected exactly one release ZIP in dist/.")
    return archives[0]

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", nargs="?", type=Path)
    parser.add_argument("--expected-commit")
    args = parser.parse_args()
    path = args.archive or find_archive()
    validate_package(path, args.expected_commit)
    print(f"PASS: {path.name}; x64 binaries, commit, profile, contents and checksums verified.")
