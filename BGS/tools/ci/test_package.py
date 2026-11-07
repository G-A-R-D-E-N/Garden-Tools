import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

BGS = Path(__file__).resolve().parents[2]
SCRIPT = Path(__file__).with_name("package.py")
COMMIT = "a" * 40


def run(*args):
    return subprocess.run([sys.executable, "-I", str(SCRIPT), *args], capture_output=True, text=True)


def write_executable(path, version, commit12):
    path.write_text(f'#!/bin/sh\necho "Behaviour Graph Studio {version} build {commit12} linux-x64"\n',
                    encoding="utf-8")
    os.chmod(path, 0o755)


class PackageTest(unittest.TestCase):
    def test_missing_native_library_is_refused_for_each_runtime(self):
        for runtime, native in (("win-x64", "radicle.dll"), ("linux-x64", "libradicle.so")):
            with self.subTest(runtime=runtime), tempfile.TemporaryDirectory(dir=BGS) as tmp:
                result = run(tmp, "--commit", COMMIT, "--runtime", runtime)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(f"Package is missing the native Radicle library {native}.", result.stderr)
                self.assertNotIn("FileNotFoundError", result.stderr)

    def test_short_commit_is_rejected_before_anything_runs(self):
        with tempfile.TemporaryDirectory(dir=BGS) as tmp:
            result = run(tmp, "--commit", "abc", "--runtime", "linux-x64")
        self.assertEqual(result.returncode, 2)
        self.assertIn("Commit must be a full lowercase Git SHA.", result.stderr)

    def test_directory_outside_bgs_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = run(tmp, "--commit", COMMIT, "--runtime", "linux-x64")
        self.assertEqual(result.returncode, 2)
        self.assertIn("Package must be an existing output directory beneath BGS.", result.stderr)

    @unittest.skipUnless(os.name == "posix", "the package executable is a shell script")
    def test_identity_pdb_build_info_and_archive(self):
        version = ET.parse(BGS / "app" / "BehaviourStudio.csproj").findtext(".//Version")
        with tempfile.TemporaryDirectory(dir=BGS) as tmp, tempfile.TemporaryDirectory() as out:
            package = Path(tmp)
            (package / "libradicle.so").write_bytes(b"")
            (package / "sub").mkdir()
            (package / "sub" / "x.pdb").write_bytes(b"")
            executable = package / "BehaviourGraphStudio"
            write_executable(executable, version, "aaaaaaaaaaaa")
            archive = Path(out) / "out.zip"

            result = run(tmp, "--commit", COMMIT, "--runtime", "linux-x64", "--archive", str(archive))

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("build aaaaaaaaaaaa", result.stdout)
            self.assertFalse((package / "sub" / "x.pdb").exists())
            self.assertEqual(json.loads((package / "build-info.json").read_text(encoding="utf-8")),
                             {"version": version, "commit": COMMIT, "runtime": "linux-x64"})
            with zipfile.ZipFile(archive) as zipped:
                names = set(zipped.namelist())
            self.assertTrue({"build-info.json", "libradicle.so", "BehaviourGraphStudio"} <= names)
            self.assertNotIn("sub/x.pdb", names)
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            self.assertEqual((Path(out) / "out.zip.sha256").read_text(encoding="utf-8"),
                             f"{digest}  out.zip\n")

            write_executable(executable, version, "bbbbbbbbbbbb")
            mismatch = run(tmp, "--commit", COMMIT, "--runtime", "linux-x64")
            self.assertNotEqual(mismatch.returncode, 0)
            self.assertIn("Published build identity mismatch", mismatch.stderr)


if __name__ == "__main__":
    unittest.main()
