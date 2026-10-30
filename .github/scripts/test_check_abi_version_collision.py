import os
import subprocess
import tempfile
import unittest

import check_abi_version_collision as check

HEADER_TEXT = ("#ifndef FO4_C_API_H\n"
               "  #define FO4_C_API_H\n"
               "  #define RADICLE_ABI_VERSION 95u\n")

BARE_TEXT = "#define RADICLE_ABI_VERSION 7\n"


def claim(number, version, head=None, base="dev-sha", head_ref="", base_ref="nomad/dev"):
    return {
        "number": number,
        "version": version,
        "head": head or "sha{}".format(number),
        "head_ref": head_ref or "branch{}".format(number),
        "base": base,
        "base_ref": base_ref,
    }


def chain(numbers, version, base_ref="nomad/dev", base_sha="dev-sha", by_ref=True):
    """Each claim is based on the previous one's head, as a stack is."""
    claims = []
    previous_ref, previous_sha = base_ref, base_sha
    for number in numbers:
        claims.append({
            "number": number,
            "version": version,
            "head": "sha{}".format(number),
            "head_ref": "branch{}".format(number),
            "base": previous_sha,
            "base_ref": previous_ref if by_ref else "",
        })
        previous_ref, previous_sha = "branch{}".format(number), "sha{}".format(number)
    return claims


class ReadVersionTest(unittest.TestCase):
    def test_reads_the_suffixed_and_bare_forms(self):
        self.assertEqual(95, check.read_version(HEADER_TEXT, "header"))
        self.assertEqual(7, check.read_version(BARE_TEXT, "header"))

    def test_names_the_file_it_could_not_read(self):
        with self.assertRaises(ValueError) as raised:
            check.read_version("#define OTHER 1\n", "Radicle/include/fo4/c_api.h")
        self.assertIn("c_api.h", str(raised.exception))


class LocalProblemsTest(unittest.TestCase):
    def test_a_bump_with_an_edited_header_passes(self):
        self.assertEqual([], check.local_problems(95, 92, ["Radicle/include/fo4/c_api_nif.h"]))

    def test_an_edited_header_that_keeps_the_base_number_fails(self):
        problems = check.local_problems(92, 92, ["Radicle/include/fo4/c_api_nif.h"])
        self.assertEqual(1, len(problems))
        self.assertIn("c_api_nif.h", problems[0])
        self.assertIn("VerifyAbi", problems[0])

    def test_a_pull_request_that_touches_no_abi_header_keeps_the_base_number(self):
        self.assertEqual([], check.local_problems(92, 92, []))

    def test_a_number_below_the_base_is_a_stale_branch(self):
        problems = check.local_problems(93, 95, [])
        self.assertEqual(1, len(problems))
        self.assertIn("backwards", problems[0])
        self.assertIn("95", problems[0])

    def test_a_stacked_child_keeping_the_parents_number_passes(self):
        self.assertEqual([], check.local_problems(
            93, 93, ["Radicle/include/fo4/c_api_nif.h"], parent_version=93))

    def test_a_dev_based_header_edit_without_a_bump_fails(self):
        problems = check.local_problems(
            93, 93, ["Radicle/include/fo4/c_api_nif.h"], parent_version=None)
        self.assertEqual(1, len(problems))
        self.assertIn("c_api_nif.h", problems[0])


class ClaimConflictsTest(unittest.TestCase):
    def test_two_independent_pulls_claiming_one_number_collide(self):
        claims = [claim(1033, 93), claim(1041, 93)]
        self.assertEqual([(93, [1033, 1041])], check.claim_conflicts(claims, 92))

    def test_a_stack_inherits_one_number_without_colliding_with_itself(self):
        self.assertEqual([], check.claim_conflicts(chain([1041, 1042, 1043, 1044], 95), 92))

    def test_a_stack_is_one_claim_even_when_only_head_shas_join_it(self):
        self.assertEqual([], check.claim_conflicts(
            chain([1041, 1042, 1043], 95, by_ref=False), 92))

    def test_an_independent_pull_claiming_the_stacks_number_collides(self):
        claims = chain([1041, 1042, 1043, 1044], 95) + [claim(1038, 95, base_ref="nomad/dev")]
        self.assertEqual([(95, [1038, 1041, 1042, 1043, 1044])],
                         check.claim_conflicts(claims, 92))

    def test_pulls_that_did_not_bump_do_not_collide_on_the_base_number(self):
        self.assertEqual([], check.claim_conflicts(
            [claim(1039, 92), claim(1040, 92, base_ref="main")], 92))

    def test_two_different_numbers_never_collide(self):
        self.assertEqual([], check.claim_conflicts(
            [claim(1033, 93), claim(1038, 94), claim(1041, 95)], 92))

    def test_a_dev_based_pull_carrying_the_parents_commits_is_one_lineage(self):
        parent = claim(1041, 95, base_ref="nomad/dev")
        child = claim(1042, 95, base_ref="nomad/dev")
        self.assertEqual([], check.claim_conflicts(
            [parent, child], 92, ancestry={1042: {1041}}))

    def test_sibling_pulls_on_one_stack_parent_collide(self):
        parent = claim(1041, 95, head_ref="stack", base_ref="nomad/dev")
        first = claim(1042, 95, head_ref="sibling-a", base_ref="stack")
        second = claim(1043, 95, head_ref="sibling-b", base_ref="stack")
        self.assertEqual([(95, [1041, 1042, 1043])], check.claim_conflicts(
            [parent, first, second], 92,
            ancestry={1042: {1041}, 1043: {1041}}))


class CompareStatusTest(unittest.TestCase):
    def run_with(self, bodies):
        seen = []

        def fake_get_json(url, token):
            seen.append(url)
            return dict(bodies[url])

        real = check.get_json
        check.get_json = fake_get_json
        try:
            return check.compare_status("a", "r", "t", "old", "new"), seen
        finally:
            check.get_json = real

    def urls(self, old, new):
        return ("a/repos/r/compare/{}...{}".format(old, new),
                "a/repos/r/compare/{}...{}".format(new, old))

    def test_counts_both_ways_before_naming_the_relation(self):
        forward, backward = self.urls("old", "new")
        for bodies, want in [({forward: {"total_commits": 1}, backward: {"total_commits": 0}}, "ahead"),
                             ({forward: {"total_commits": 0}, backward: {"total_commits": 2}}, "behind"),
                             ({forward: {"total_commits": 0}, backward: {"total_commits": 0}}, "identical"),
                             ({forward: {"total_commits": 1}, backward: {"total_commits": 1}}, "diverged"),
                             ({forward: {}, backward: {"total_commits": 0}}, None),
                             ({forward: {"total_commits": None}, backward: {"total_commits": 1}}, None),
                             ({forward: {"total_commits": "1"}, backward: {"total_commits": 0}}, None)]:
            status, seen = self.run_with(bodies)
            self.assertEqual(want, status)
            self.assertEqual([forward, backward], seen)

    def test_a_failed_request_gives_no_ancestry(self):
        import urllib.error
        real = check.get_json
        check.get_json = lambda url, token: (_ for _ in ()).throw(urllib.error.URLError("down"))
        try:
            self.assertIsNone(check.compare_status("a", "r", "t", "old", "new"))
        finally:
            check.get_json = real


class ChangedHeadersTest(unittest.TestCase):
    def make_repo(self, root, header_text):
        run = lambda *args: subprocess.run(
            ("git",) + args, check=True, capture_output=True, text=True, cwd=root)
        run("init", "-b", "main")
        run("config", "core.hooksPath", "")
        run("config", "user.email", "t@t")
        run("config", "user.name", "t")
        header = os.path.join(root, "Radicle", "include", "fo4", "c_api_nif.h")
        os.makedirs(os.path.dirname(header))
        with open(header, "w") as stream:
            stream.write(header_text)
        run("add", ".")
        run("commit", "-m", "base")
        return run

    def test_a_branch_behind_its_base_reports_no_header_edit(self):
        with tempfile.TemporaryDirectory() as root:
            run = self.make_repo(root, "#define RADICLE_ABI_VERSION 92u\n")
            run("checkout", "-b", "behind")
            run("checkout", "main")
            header = os.path.join(root, "Radicle", "include", "fo4", "c_api_nif.h")
            with open(header, "a") as stream:
                stream.write("/* base header edit */\n")
            run("commit", "-am", "base header edit")
            here = os.getcwd()
            os.chdir(root)
            try:
                self.assertEqual([], check.changed_headers_from("main", "behind"))
                self.assertEqual(["Radicle/include/fo4/c_api_nif.h"],
                                 check.changed_headers_from("behind", "main"))
            finally:
                os.chdir(here)

    def test_a_missing_base_gives_exit_two(self):
        with tempfile.TemporaryDirectory() as root:
            run = self.make_repo(root, "#define RADICLE_ABI_VERSION 92u\n")
            header = os.path.join(root, "Radicle", "include", "fo4", "c_api.h")
            with open(header, "w") as stream:
                stream.write("#define RADICLE_ABI_VERSION 92u\n")
            run("add", ".")
            run("commit", "-m", "number header")
            here = os.getcwd()
            os.chdir(root)
            try:
                self.assertEqual(2, check.main(["--base", "gone", "--local-only"]))
            finally:
                os.chdir(here)

    def test_a_head_with_no_number_gives_exit_two(self):
        with tempfile.TemporaryDirectory() as root:
            self.make_repo(root, "/* no number here */\n")
            here = os.getcwd()
            os.chdir(root)
            try:
                self.assertEqual(2, check.main(["--base", "main", "--local-only"]))
            finally:
                os.chdir(here)


if __name__ == "__main__":
    unittest.main()
