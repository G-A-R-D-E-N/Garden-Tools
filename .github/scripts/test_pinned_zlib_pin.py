import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class PinnedZlibPinTest(unittest.TestCase):
    def test_cmake_zlib_pin_matches_the_script_pattern(self):
        script = (ROOT / ".github" / "scripts" / "pinned-zlib-windows.ps1").read_text(encoding="utf-8")
        pattern = re.search(r"Matches\(\$cmakeText, '([^']+)'\)", script).group(1)
        cmake = (ROOT / "PluginEditTool" / "native" / "CMakeLists.txt").read_text(encoding="utf-8")
        pins = re.findall(pattern, cmake)
        self.assertEqual(len(pins), 1)
        self.assertRegex(pins[0], r"^[0-9a-f]{40}$")
        workflow = (ROOT / ".github" / "workflows" / "bgs-ci.yml").read_text(encoding="utf-8")
        self.assertIn(pattern, workflow)


if __name__ == "__main__":
    unittest.main()
