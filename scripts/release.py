# SPDX-License-Identifier: BSD-3-Clause
"""Publish only the verified artifact from the successful tagged build."""
import argparse
import json
import os
import subprocess
from verify_package import find_archive, sha, validate_package
from version import ROOT, check_tag, release_version

def gh(*args, capture=False, check=True):
    return subprocess.run(["gh", *map(str, args)], check=check, text=True,
                          stdout=subprocess.PIPE if capture else None,
                          stderr=subprocess.PIPE if capture else None)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args()
    repo = os.environ["GITHUB_REPOSITORY"]
    if repo != "brpjerry/eldenringrt-freefps":
        raise SystemExit("Release publishing is restricted to the project repository.")
    check_tag(args.tag)
    archive = find_archive()
    manifest = validate_package(archive, args.commit)
    ref = gh("api", f"repos/{repo}/git/ref/tags/{args.tag}", capture=True)
    obj = json.loads(ref.stdout)["object"]
    if obj["type"] == "tag":
        obj = json.loads(gh("api", f"repos/{repo}/git/tags/{obj['sha']}", capture=True).stdout)["object"]
    if obj["type"] != "commit" or obj["sha"] != args.commit:
        raise SystemExit("Release tag does not point to the artifact's source commit.")
    previous = gh("release", "view", args.tag, "--repo", repo, "--json", "isDraft,targetCommitish", capture=True, check=False)
    if previous.returncode == 0:
        existing = json.loads(previous.stdout)
        if not existing["isDraft"] or existing["targetCommitish"] != args.commit:
            raise SystemExit("An existing published or differently targeted release will not be replaced.")
    else:
        options = ["--prerelease"] if release_version()["prerelease"] else []
        gh("release", "create", args.tag, "--repo", repo, "--verify-tag", "--target", args.commit,
           "--draft", "--title", f"RT Batching {manifest['version']} (experimental)",
           "--notes-file", ROOT / "docs" / "RELEASE_NOTES.md", *options)
    assets = [archive, archive.with_suffix(".zip.sha256")]
    gh("release", "upload", args.tag, *assets, "--repo", repo, "--clobber")
    release = json.loads(gh("api", f"repos/{repo}/releases/tags/{args.tag}", capture=True).stdout)
    remote_assets = {a["name"]: a for a in release["assets"]}
    if set(remote_assets) != {p.name for p in assets}:
        raise SystemExit("Draft release has unexpected assets; leaving it unpublished.")
    for path in assets:
        if remote_assets[path.name].get("digest") != "sha256:" + sha(path.read_bytes()):
            raise SystemExit("Uploaded asset digest mismatch; leaving draft unpublished.")
    gh("release", "edit", args.tag, "--repo", repo, "--draft=false")
    print(release["html_url"])

if __name__ == "__main__":
    main()
