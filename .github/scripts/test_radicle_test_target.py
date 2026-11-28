import os
import subprocess
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().with_name("radicle_test_target.sh")
IDENTITY = {
    "GIT_AUTHOR_NAME": "test",
    "GIT_AUTHOR_EMAIL": "test@example.invalid",
    "GIT_COMMITTER_NAME": "test",
    "GIT_COMMITTER_EMAIL": "test@example.invalid",
}
LISTS = {
    "Radicle/cmake/tests_core.cmake": "set(FO4_TESTS_CORE\n  tests/test_core_a.cpp\n)\n",
    "Radicle/cmake/tests_papyrus.cmake": "set(FO4_TESTS_PAPYRUS\n  tests/test_papyrus_a.cpp\n)\n",
    "Radicle/cmake/tests_flash.cmake": "set(FO4_TESTS_FLASH\n  tests/test_flash_a.cpp\n)\n",
    "Radicle/cmake/tests_assets.cmake": "set(FO4_TESTS_ASSETS\n  tests/test_assets_a.cpp\n)\n",
}
BASE = {
    **LISTS,
    "Radicle/tests/test_core_a.cpp": "core\n",
    "Radicle/tests/test_papyrus_a.cpp": "papyrus\n",
    "Radicle/tests/test_flash_a.cpp": "flash\n",
    "Radicle/tests/test_assets_a.cpp": "assets\n",
    "Radicle/tests/test_unlisted.cpp": "unlisted\n",
    "Radicle/include/fo4/api.h": "api\n",
    "Radicle/src/papyrus/papyrus.cpp": "src\n",
}


def git(repo, *args, env=None, text=None):
    return subprocess.run(["git", "-C", str(repo), *args], check=True, capture_output=True,
                          text=True, input=text, env=env).stdout.strip()


def parentless_commit(repo, files):
    env = dict(os.environ, GIT_INDEX_FILE=str(repo / ".git" / "index.tmp"), **IDENTITY)
    git(repo, "read-tree", "--empty", env=env)
    for path, text in files.items():
        blob = git(repo, "hash-object", "-w", "--stdin", text=text)
        git(repo, "update-index", "--add", "--cacheinfo", f"100644,{blob},{path}", env=env)
    tree = git(repo, "write-tree", env=env)
    return git(repo, "commit-tree", tree, "-m", "change", env=env)


class RadicleTestTargetTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.origin = self.root / "origin"
        self.pr = self.root / "pr"
        self.origin.mkdir()
        self.pr.mkdir()
        git(self.origin, "init", "-q")
        base = parentless_commit(self.origin, BASE)
        git(self.origin, "update-ref", "refs/heads/ci/nomad/dev", base)
        git(self.pr, "init", "-q")
        git(self.pr, "remote", "add", "origin", self.origin.as_uri())

    def scope(self, changes):
        head = parentless_commit(self.pr, {**BASE, **changes})
        git(self.pr, "update-ref", "HEAD", head)
        git(self.pr, "reset", "--hard", "-q")
        result = subprocess.run(["bash", str(SCRIPT), "ci/pr-1"], cwd=self.pr, check=True,
                                capture_output=True, text=True)
        return result.stdout.strip()

    def test_core_test_only_change_builds_core(self):
        self.assertEqual("fo4_tests_core", self.scope({"Radicle/tests/test_core_a.cpp": "changed\n"}))

    def test_papyrus_test_only_change_builds_papyrus(self):
        self.assertEqual("fo4_tests_papyrus",
                         self.scope({"Radicle/tests/test_papyrus_a.cpp": "changed\n"}))

    def test_mixed_groups_build_all(self):
        self.assertEqual("all", self.scope({
            "Radicle/tests/test_core_a.cpp": "changed\n",
            "Radicle/tests/test_papyrus_a.cpp": "changed\n",
        }))

    def test_unlisted_test_file_builds_all(self):
        self.assertEqual("all", self.scope({"Radicle/tests/test_unlisted.cpp": "changed\n"}))

    def test_empty_diff_builds_all(self):
        self.assertEqual("all", self.scope({}))

    def test_failed_fetch_builds_all(self):
        git(self.pr, "remote", "set-url", "origin", (self.root / "missing").as_uri())
        self.assertEqual("all", self.scope({"Radicle/tests/test_core_a.cpp": "changed\n"}))

    def test_cmake_change_builds_all(self):
        self.assertEqual("all", self.scope({"Radicle/cmake/tests_core.cmake": LISTS["Radicle/cmake/tests_core.cmake"] + "# x\n"}))

    def test_public_header_change_builds_all(self):
        self.assertEqual("all", self.scope({"Radicle/include/fo4/api.h": "changed\n"}))

    def test_papyrus_source_change_builds_all(self):
        self.assertEqual("all", self.scope({"Radicle/src/papyrus/papyrus.cpp": "changed\n"}))


if __name__ == "__main__":
    unittest.main()
