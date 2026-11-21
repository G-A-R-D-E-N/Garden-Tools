#!/usr/bin/env python3
"""Tests for header discovery in generate_interop.py."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_interop  # noqa: E402

INCLUDE_DIR = Path(__file__).resolve().parents[3] / 'include'


def on_disk(include_dir):
    return {f'fo4/{path.name}' for path in (include_dir / 'fo4').glob('c_api*.h')}


class DiscoverHeadersTest(unittest.TestCase):
    def test_every_header_on_disk_is_generated_or_excluded(self):
        generated = set(generate_interop.discover_headers(INCLUDE_DIR))
        self.assertEqual(on_disk(INCLUDE_DIR), generated | generate_interop.EXCLUDE)
        self.assertFalse(generated & generate_interop.EXCLUDE)

    def test_excluded_headers_exist_on_disk(self):
        self.assertLessEqual(generate_interop.EXCLUDE, on_disk(INCLUDE_DIR))

    def test_new_header_is_generated_after_the_ordered_ones_in_sorted_order(self):
        with tempfile.TemporaryDirectory() as temporary:
            include_dir = Path(temporary)
            (include_dir / 'fo4').mkdir()
            for header in [*generate_interop.HEADER_ORDER, 'fo4/c_api_zzz.h', 'fo4/c_api_aaa.h']:
                (include_dir / header).write_text('', encoding='utf-8')
            found = generate_interop.discover_headers(include_dir)
        self.assertEqual(found, [*generate_interop.HEADER_ORDER, 'fo4/c_api_aaa.h', 'fo4/c_api_zzz.h'])

    def test_ordered_header_missing_from_disk_stops_the_generator(self):
        with tempfile.TemporaryDirectory() as temporary:
            (Path(temporary) / 'fo4').mkdir()
            with self.assertRaises(SystemExit):
                generate_interop.discover_headers(Path(temporary))


if __name__ == '__main__':
    unittest.main()
