import unittest

import check_abi_version_collision as check

HEADER_TEXT = ("#ifndef FO4_C_API_H\n"
               "  #define RADICLE_ABI_MAJOR 1u\n"
               "#define RADICLE_ABI_MINOR 517u // note\r\n"
               "#define RADICLE_ABI_VERSION (RADICLE_ABI_MAJOR * 100000u + RADICLE_ABI_MINOR)\n")
HEADERS = ["Radicle/include/fo4/c_api.h"]


class ReadVersionTest(unittest.TestCase):
    def test_reads_major_and_minor_with_suffix_indent_comment_and_crlf(self):
        self.assertEqual((1, 517), check.read_version(HEADER_TEXT))

    def test_the_old_single_integer_header_has_no_version(self):
        self.assertIsNone(check.read_version("#define RADICLE_ABI_VERSION 516u\n"))

    def test_a_header_with_only_one_part_has_no_version(self):
        self.assertIsNone(check.read_version("#define RADICLE_ABI_MAJOR 1u\n"))

    def test_an_expression_is_not_a_plain_number(self):
        self.assertIsNone(check.read_version("#define RADICLE_ABI_MAJOR 1u\n#define RADICLE_ABI_MINOR (2u + 3u)\n"))


MINOR_BUMP_DIFF = """\
--- a/Radicle/include/fo4/c_api.h
+++ b/Radicle/include/fo4/c_api.h
@@ -14,4 +14,4 @@
-{OPEN} old rule text
-
+{OPEN} new rule text
+
 typedef int unchanged_t;
-#define RADICLE_ABI_MINOR 517u
+#define RADICLE_ABI_MINOR 518u
@@ -45,0 +46 @@
+RADICLE_API uint32_t radicle_new_export(void);
""".replace("{OPEN}", "/" + "*")

SIGNATURE_DIFF = MINOR_BUMP_DIFF + """\
@@ -60 +61 @@
-int a(void);
+int a(int);
"""


class DeletedLinesTest(unittest.TestCase):
    def test_version_defines_comments_blanks_and_additions_count_nothing(self):
        self.assertEqual(0, check.deleted_lines(MINOR_BUMP_DIFF))

    def test_a_deleted_signature_counts(self):
        self.assertEqual(1, check.deleted_lines("--- a/x.h\n+++ b/x.h\n@@ -1 +1 @@\n-int a(void);\n+int a(int);\n"))

    def test_an_empty_diff_counts_nothing(self):
        self.assertEqual(0, check.deleted_lines(""))

    def test_a_minor_bump_diff_needs_no_major(self):
        self.assertEqual([], check.problems((1, 518), (1, 517), HEADERS, check.deleted_lines(MINOR_BUMP_DIFF)))

    def test_the_same_diff_with_a_deleted_signature_needs_a_major(self):
        found = check.problems((1, 518), (1, 517), HEADERS, check.deleted_lines(SIGNATURE_DIFF))
        self.assertEqual(1, len(found))
        self.assertIn("bump RADICLE_ABI_MAJOR", found[0])


class ProblemsTest(unittest.TestCase):
    def test_a_missing_version_fails(self):
        self.assertEqual(1, len(check.problems(None, (1, 517), HEADERS)))

    def test_a_minor_at_the_limit_fails(self):
        self.assertEqual(1, len(check.problems((1, check.MINOR_LIMIT), (1, 517), HEADERS)))

    def test_a_lower_minor_with_a_changed_header_fails(self):
        self.assertEqual(1, len(check.problems((1, 516), (1, 517), HEADERS)))

    def test_a_lower_version_without_a_header_change_passes(self):
        self.assertEqual([], check.problems((1, 516), (1, 517), []))

    def test_unchanged_headers_with_the_same_version_pass(self):
        self.assertEqual([], check.problems((1, 517), (1, 517), []))

    def test_a_changed_header_with_the_same_version_fails(self):
        found = check.problems((1, 517), (1, 517), HEADERS)
        self.assertEqual(1, len(found))
        self.assertIn("keeps ABI 1.517; bump RADICLE_ABI_MINOR (or MAJOR)", found[0])

    def test_a_changed_header_with_a_minor_bump_passes(self):
        self.assertEqual([], check.problems((1, 518), (1, 517), HEADERS))

    def test_a_downgrade_with_a_changed_header_fails(self):
        self.assertEqual(1, len(check.problems((1, 516), (1, 517), HEADERS)))
        self.assertEqual(1, len(check.problems((0, 900), (1, 517), HEADERS)))

    def test_deleted_header_lines_need_a_major_bump(self):
        self.assertEqual(1, len(check.problems((1, 518), (1, 517), HEADERS, deleted=3)))
        self.assertEqual([], check.problems((2, 0), (1, 517), HEADERS, deleted=3))
        self.assertEqual([], check.problems((1, 518), (1, 517), HEADERS, deleted=0))

    def test_a_higher_major_may_restart_the_minor(self):
        self.assertEqual([], check.problems((2, 0), (1, 517), HEADERS))

    def test_a_base_without_major_and_minor_cannot_be_compared(self):
        self.assertEqual([], check.problems((1, 517), None, HEADERS))


if __name__ == "__main__":
    unittest.main()
