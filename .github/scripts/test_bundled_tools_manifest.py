import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PLUGIN = ROOT / "PluginEditTool"


class BundledToolsManifestTest(unittest.TestCase):
    def setUp(self):
        manifest = json.loads((PLUGIN / "tools" / "bundled-tools.json").read_text(encoding="utf-8"))
        self.tools = {tool["id"]: tool for tool in manifest["tools"]}

    def test_unpinned_or_non_https_asset_is_rejected(self):
        self.assertEqual({"texconv", "ffmpeg"}, set(self.tools))
        for tool in self.tools.values():
            for key in ("name", "version", "license", "file", "url", "licenseUrl", "sha256"):
                self.assertTrue(tool[key], f"{tool['id']} has no {key}")
            self.assertRegex(tool["sha256"], r"^[0-9a-f]{64}$")
            for url in (tool["url"], tool["licenseUrl"]):
                self.assertTrue(url.startswith("https://"), url)
                self.assertNotIn("latest", url.lower(), url)

    def test_short_lived_ffmpeg_pin_documents_its_expiry(self):
        self.assertTrue(self.tools["ffmpeg"].get("pinNote"))

    def test_archive_member_is_set_exactly_for_zip_downloads(self):
        for tool in self.tools.values():
            self.assertEqual(tool["url"].endswith(".zip"), tool["member"] is not None, tool["id"])

    def test_install_paths_match_the_locator_lookup(self):
        locator = (PLUGIN / "native" / "src" / "tools" / "ToolLocator.cpp").read_text(encoding="utf-8")
        for tool in self.tools.values():
            literal = ", ".join(f'"{part}"' for part in tool["file"].split("/"))
            self.assertIn(f"appPath({{{literal}}})", locator, tool["file"])

    def test_package_fetches_pinned_tools_and_release_lane_does_not_bundle_them(self):
        package = (PLUGIN / "package.ps1").read_text(encoding="utf-8")
        self.assertIn("-Id texconv", package)
        self.assertIn("-Id ffmpeg", package)
        workflow = (ROOT / ".forgejo" / "workflows" / "release.yml").read_text(encoding="utf-8")
        self.assertIsNone(re.search(r"FO4IDE_TEXCONV_PATH", workflow))


if __name__ == "__main__":
    unittest.main()
