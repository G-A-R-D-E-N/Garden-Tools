#!/usr/bin/env python3
"""Tests for check_project_structure.py: list, forwarder and include-cycle checks on temporary trees."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_project_structure as guard  # noqa: E402


def build_tree(directory, files):
    root = Path(directory)
    for name in ("CMakeLists.txt", "cmake/sources_core.cmake", "cmake/tests_core.cmake"):
        files.setdefault(name, "")
    for name, text in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    (root / "src").mkdir(exist_ok=True)
    (root / "tests").mkdir(exist_ok=True)
    (root / "include" / "fo4").mkdir(parents=True, exist_ok=True)
    return root


class ListTests(unittest.TestCase):
    def check(self, files):
        with tempfile.TemporaryDirectory() as directory:
            return guard.list_violations(build_tree(directory, files))

    def test_listed_source_and_test_are_clean(self):
        self.assertEqual([], self.check({
            "src/a.cpp": "", "tests/test_a.cpp": "",
            "cmake/sources_core.cmake": "set(FO4_SOURCES_CORE\n  src/a.cpp\n)\n",
            "cmake/tests_core.cmake": "set(FO4_TESTS_CORE\n  tests/test_a.cpp\n)\n",
        }))

    def test_unlisted_source_is_one_violation(self):
        problems = self.check({"src/a.cpp": ""})
        self.assertEqual(1, len(problems))
        self.assertIn("src/a.cpp is not in cmake/sources_*.cmake", problems[0])

    def test_unlisted_test_is_one_violation(self):
        problems = self.check({"tests/test_a.cpp": ""})
        self.assertEqual(1, len(problems))
        self.assertIn("tests/test_a.cpp is not in cmake/tests_*.cmake", problems[0])

    def test_stale_entry_is_one_violation(self):
        problems = self.check({"cmake/sources_core.cmake": "set(X src/gone.cpp)\n"})
        self.assertEqual(1, len(problems))
        self.assertIn("does not exist", problems[0])

    def test_platform_source_named_by_cmakelists_counts_as_listed(self):
        self.assertEqual([], self.check({
            "src/w.cpp": "", "CMakeLists.txt": "list(APPEND FO4_SOURCES_CORE src/w.cpp)\n"}))

    def test_nested_src_path_is_not_read_as_radicle_src(self):
        problems = self.check({
            "src/x.cpp": "", "cmake/sources_core.cmake": "set(X tools/foo/src/x.cpp)\n"})
        self.assertEqual(1, len(problems))
        self.assertIn("src/x.cpp is not in cmake/sources_*.cmake", problems[0])

    def test_variable_prefixed_src_path_is_read(self):
        self.assertEqual([], self.check({
            "src/x.cpp": "", "cmake/sources_core.cmake": "set(X ${CMAKE_CURRENT_SOURCE_DIR}/src/x.cpp)\n"}))

    def test_cmakelists_mention_that_adds_no_source_is_not_listing(self):
        problems = self.check({
            "src/w.cpp": "",
            "CMakeLists.txt": 'if(EXISTS src/w.cpp)\nmessage(STATUS "see src/w.cpp")\nlist(REMOVE_ITEM X src/w.cpp)\nendif()\n'})
        self.assertEqual(1, len(problems))
        self.assertIn("src/w.cpp is not in cmake/sources_*.cmake", problems[0])

    def test_annotation_escapes_workflow_command_characters(self):
        text = guard.annotation("src/a,b:c%d.cpp", "bad\nline: %")
        self.assertEqual("::error file=src/a%2Cb%3Ac%25d.cpp::bad%0Aline: %25", text)
        self.assertNotIn("\n", guard.annotation(None, "a\r\nb"))


class ForwarderTests(unittest.TestCase):
    def check(self, files):
        with tempfile.TemporaryDirectory() as directory:
            return guard.forwarder_violations(build_tree(directory, files))

    def test_new_one_include_shim_is_one_violation(self):
        problems = self.check({"include/fo4/forms/shim.hpp": '#pragma once\n#include "fo4/forms/real.hpp"\n'})
        self.assertEqual(1, len(problems))
        self.assertIn("include/fo4/forms/shim.hpp", problems[0])

    def test_commented_shim_is_still_a_violation(self):
        text = '// moved\n// to real.hpp\n/* old home\n   of the decl */\n#pragma once\n# include "fo4/forms/real.hpp"\n'
        self.assertEqual(1, len(self.check({"include/fo4/forms/shim.hpp": text})))

    def test_baselined_shim_is_allowed(self):
        self.assertEqual([], self.check({
            "include/fo4/plugin/container.hpp": '#pragma once\n#include "fo4/plugin_core/container.hpp"\n'}))

    def test_header_with_declarations_or_two_includes_is_not_a_shim(self):
        self.assertEqual([], self.check({
            "include/fo4/forms/a.hpp": '#pragma once\n#include "fo4/forms/b.hpp"\nstruct A {};\n',
            "include/fo4/forms/c.hpp": '#pragma once\n#include "fo4/forms/b.hpp"\n#include "fo4/forms/d.hpp"\n',
        }))


class CycleTests(unittest.TestCase):
    def graph_and_problems(self, files):
        with tempfile.TemporaryDirectory() as directory:
            root = build_tree(directory, files)
            return guard.include_graph(root), guard.cycle_violations(root)

    def test_graph_reads_module_includes(self):
        graph, problems = self.graph_and_problems({"src/forms/a.cpp": '#include "fo4/nif/x.hpp"\n'})
        self.assertEqual({"forms": {"nif"}}, graph)
        self.assertEqual([], problems)

    def test_three_module_cycle_is_one_violation(self):
        _, problems = self.graph_and_problems({
            "src/forms/a.cpp": '#include "fo4/nif/x.hpp"\n',
            "src/nif/b.cpp": '#include "fo4/lod/y.hpp"\n',
            "src/lod/c.cpp": '#include "fo4/forms/z.hpp"\n',
        })
        self.assertEqual(1, len(problems))
        self.assertIn("forms -> ", problems[0])

    def test_baselined_cycle_is_allowed(self):
        _, problems = self.graph_and_problems({
            "src/environment/a.cpp": '#include "fo4/forms/x.hpp"\n',
            "include/fo4/forms/y.hpp": '#include "fo4/environment/z.hpp"\n',
        })
        self.assertEqual([], problems)

    def test_cycle_through_a_private_src_header_is_found(self):
        _, problems = self.graph_and_problems({
            "src/forms/a.cpp": '#include "fo4/nif/x.hpp"\n',
            "src/nif/private.hpp": '#include "fo4/forms/y.hpp"\n',
        })
        self.assertEqual(1, len(problems))

    def test_cycle_through_a_root_level_header_is_found(self):
        graph, problems = self.graph_and_problems({
            "src/forms/a.cpp": '#include "fo4/formid.hpp"\n',
            "include/fo4/formid.hpp": '#include "fo4/forms/y.hpp"\n',
        })
        self.assertEqual({"forms": {"(root)"}, "(root)": {"forms"}}, graph)
        self.assertEqual(1, len(problems))

    def test_relative_and_angle_includes_are_read(self):
        graph, _ = self.graph_and_problems({
            "src/plugin/a.cpp": '#include "../binary/cursor.hpp"\n#include <fo4/schema/scalar.hpp>\n'})
        self.assertEqual({"plugin": {"binary", "schema"}}, graph)

    def test_top_level_src_files_and_the_umbrella_header_do_not_form_cycles(self):
        graph, problems = self.graph_and_problems({
            "src/c_api.cpp": '#include "fo4/forms/x.hpp"\n',
            "include/fo4/formid.hpp": "",
            "include/fo4/fo4.hpp": '#include "fo4/forms/x.hpp"\n',
            "include/fo4/forms/x.hpp": '#include "fo4/formid.hpp"\n',
        })
        self.assertEqual({"(src)": {"forms"}, "forms": {"(root)"}}, graph)
        self.assertEqual([], problems)


if __name__ == "__main__":
    unittest.main()
