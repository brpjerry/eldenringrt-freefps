# SPDX-License-Identifier: BSD-3-Clause
"""Publish only the verified artifact from the successful tagged build."""
import argparse
import json
import os
import subprocess
import time
from verify_package import find_archive, sha, validate_package
from version import ROOT, check_tag, release_version

def gh(*args, capture=False, check=True):
    return subprocess.run(["gh", *map(str, args)], check=check, text=True,
                          stdout=subprocess.PIPE if capture else None,
                          stderr=subprocess.PIPE if capture else None)

def find_release(repo, tag):
    # The by-tag endpoint only returns published releases. List releases to find drafts.
    pages = json.loads(gh("api", f"repos/{repo}/releases", "--paginate", "--slurp", capture=True).stdout)
    matches = [release for page in pages for release in page if release["tag_name"] == tag]
    if len(matches) > 1:
        raise SystemExit("Multiple releases use this tag; refusing ambiguous publication.")
    return matches[0] if matches else None

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
    existing = find_release(repo, args.tag)
    if existing:
        if not existing["draft"] or existing["target_commitish"] != args.commit:
            raise SystemExit("An existing published or differently targeted release will not be replaced.")
    else:
        config = release_version()
        options = ["--prerelease"] if config["prerelease"] else []
        title = f"RT Batching {manifest['version']}" + (" (experimental)" if config["prerelease"] else "")
        gh("release", "create", args.tag, "--repo", repo, "--verify-tag", "--target", args.commit,
           "--draft", "--title", title,
           "--notes-file", ROOT / "docs" / "RELEASE_NOTES.md", *options)
        # Draft creation can return before the release-list endpoint reflects it.
        for attempt in range(11):
            existing = find_release(repo, args.tag)
            if existing is not None:
                break
            if attempt < 10:
                time.sleep(2)
        if existing is None:
            raise SystemExit("Created draft could not be found; rerun this job to resume publication.")
    release_endpoint = f"repos/{repo}/releases/{existing['id']}"
    assets = [archive, archive.with_suffix(".zip.sha256")]
    gh("release", "upload", args.tag, *assets, "--repo", repo, "--clobber")
    release = json.loads(gh("api", release_endpoint, capture=True).stdout)
    remote_assets = {a["name"]: a for a in release["assets"]}
    if set(remote_assets) != {p.name for p in assets}:
        raise SystemExit("Draft release has unexpected assets; leaving it unpublished.")
    for path in assets:
        if remote_assets[path.name].get("digest") != "sha256:" + sha(path.read_bytes()):
            raise SystemExit("Uploaded asset digest mismatch; leaving draft unpublished.")
    gh("release", "edit", args.tag, "--repo", repo, "--draft=false")
    published = json.loads(gh("api", release_endpoint, capture=True).stdout)
    if published["draft"]:
        raise SystemExit("Release is still a draft.")
    print(published["html_url"])

if __name__ == "__main__":
    main()
