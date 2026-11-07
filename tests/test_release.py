# SPDX-License-Identifier: BSD-3-Clause
"""Exercise draft lookup and ensure digest validation precedes publication."""
from pathlib import Path
import copy
import hashlib
import json
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import release

class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.archive = Path(self.directory.name) / "package.zip"
        self.archive.write_bytes(b"verified artifact")
        self.sidecar = self.archive.with_suffix(".zip.sha256")
        self.sidecar.write_bytes(b"verified checksum")
        self.tag = "v" + release.release_version()["version"]
        self.commit = "a" * 40
        self.repo = "brpjerry/eldenringrt-freefps"
        self.record = None
        self.mutations = []
        self.bad_digest = False

    def draft(self):
        return {"id": 123, "tag_name": self.tag, "draft": True,
                "target_commitish": self.commit, "assets": [], "html_url": "draft"}

    def gh(self, *args, capture=False, check=True):
        result = None
        if args[0] == "api":
            endpoint = args[1]
            if endpoint == f"repos/{self.repo}/git/ref/tags/{self.tag}":
                result = {"object": {"type": "commit", "sha": self.commit}}
            elif endpoint == f"repos/{self.repo}/releases":
                self.assertEqual(args[2:], ("--paginate", "--slurp"))
                # Relevant draft is on a later page; lookup must handle pagination.
                result = [[{"tag_name": "unrelated"}], [self.record] if self.record else []]
            elif endpoint == f"repos/{self.repo}/releases/123":
                result = copy.deepcopy(self.record)
            else:
                self.fail(f"Unexpected API endpoint: {endpoint}")
        elif args[:2] == ("release", "create"):
            self.mutations.append("create")
            self.record = self.draft()
        elif args[:2] == ("release", "upload"):
            self.mutations.append("upload")
            self.record["assets"] = [
                {"name": p.name, "digest": "sha256:" + hashlib.sha256(p.read_bytes()).hexdigest()}
                for p in (self.archive, self.sidecar)]
            if self.bad_digest:
                self.record["assets"][0]["digest"] = "sha256:" + "0" * 64
        elif args[:2] == ("release", "edit"):
            self.mutations.append("publish")
            self.record["draft"] = False
            self.record["html_url"] = f"https://github.com/{self.repo}/releases/tag/{self.tag}"
        else:
            self.fail(f"Unexpected gh command: {args}")
        return SimpleNamespace(returncode=0, stdout=json.dumps(result), stderr="")

    def invoke(self):
        with patch.dict("os.environ", {"GITHUB_REPOSITORY": self.repo}), \
             patch.object(sys, "argv", ["release.py", "--tag", self.tag, "--commit", self.commit]), \
             patch.object(release, "find_archive", return_value=self.archive), \
             patch.object(release, "validate_package", return_value={"version": self.tag[1:]}), \
             patch.object(release, "gh", side_effect=self.gh), patch("builtins.print"):
            release.main()

    def test_new_draft_is_verified_before_publishing(self):
        self.invoke()
        self.assertEqual(self.mutations, ["create", "upload", "publish"])
        self.assertFalse(self.record["draft"])

    def test_existing_matching_draft_can_resume(self):
        self.record = self.draft()
        self.invoke()
        self.assertEqual(self.mutations, ["upload", "publish"])

    def test_bad_digest_stays_unpublished(self):
        self.bad_digest = True
        with self.assertRaisesRegex(SystemExit, "digest mismatch"):
            self.invoke()
        self.assertEqual(self.mutations, ["create", "upload"])
        self.assertTrue(self.record["draft"])

    def test_published_or_differently_targeted_release_is_not_changed(self):
        for draft, target in [(False, self.commit), (True, "b" * 40)]:
            with self.subTest(draft=draft, target=target):
                self.record = self.draft()
                self.record.update(draft=draft, target_commitish=target)
                with self.assertRaisesRegex(SystemExit, "will not be replaced"):
                    self.invoke()
                self.assertEqual(self.mutations, [])

if __name__ == "__main__":
    unittest.main()
