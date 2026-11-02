import base64
import contextlib
import io
import json
import os
import subprocess
import tempfile
import unittest
import urllib.error

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

    def test_reads_suffix_case_comment_and_crlf_forms(self):
        self.assertEqual(95, check.read_version("#define RADICLE_ABI_VERSION 95UL\n", "header"))
        self.assertEqual(95, check.read_version("#define RADICLE_ABI_VERSION 95u // note\n", "header"))
        self.assertEqual(95, check.read_version("#define RADICLE_ABI_VERSION 95\r\n", "header"))

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


class InheritedClaimTest(unittest.TestCase):
    def stack(self):
        return [claim(1049, 102, head_ref="stack", base_ref="nomad/dev"),
                claim(1081, 102, head_ref="child-a", base_ref="stack"),
                claim(1082, 102, head_ref="child-b", base_ref="stack"),
                claim(1086, 102, head_ref="grandchild", base_ref="child-a")]

    # Detects: children that never edit the C API failing their parent's gate for carrying its number.
    def test_children_that_leave_the_header_alone_hold_no_claim(self):
        kept = check.without_inherited(self.stack(), lambda number: False)
        self.assertEqual([1049], [item["number"] for item in kept])
        self.assertEqual([], check.claim_conflicts(kept, 100))

    # Detects: two siblings adding different exports under one number passing the gate.
    def test_siblings_that_edit_the_header_still_collide(self):
        kept = check.without_inherited(self.stack(), lambda number: number in (1081, 1082))
        self.assertEqual([(102, [1049, 1081, 1082])], check.claim_conflicts(
            kept, 100, ancestry={1081: {1049}, 1082: {1049}}))

    def test_a_bumped_child_keeps_its_claim_without_reading_its_files(self):
        claims = [claim(1049, 102, head_ref="stack", base_ref="nomad/dev"),
                  claim(1081, 103, head_ref="child-a", base_ref="stack")]
        kept = check.without_inherited(claims, lambda number: self.fail("no read needed"))
        self.assertEqual([1049, 1081], [item["number"] for item in kept])


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
            run = self.make_repo(root, "#define RADICLE_ABI_VERSION 92u\n")
            header = os.path.join(root, "Radicle", "include", "fo4", "c_api.h")
            with open(header, "w") as stream:
                stream.write("/* no number here */\n")
            run("add", ".")
            run("commit", "-m", "numberless header")
            here = os.getcwd()
            os.chdir(root)
            try:
                self.assertEqual(2, check.main(["--base", "main", "--local-only"]))
            finally:
                os.chdir(here)

    def test_a_base_with_no_number_gives_exit_two(self):
        with tempfile.TemporaryDirectory() as root:
            origin = os.path.join(root, "origin")
            subprocess.run(("git", "init", "--bare", "origin"), check=True,
                           capture_output=True, text=True, cwd=root)
            work = os.path.join(root, "work")
            os.makedirs(work)
            run = lambda *args: subprocess.run(
                ("git",) + args, check=True, capture_output=True, text=True, cwd=work)
            run("init", "-b", "main")
            run("config", "core.hooksPath", "")
            run("config", "user.email", "t@t")
            run("config", "user.name", "t")
            run("remote", "add", "origin", origin)
            header = os.path.join(work, "Radicle", "include", "fo4", "c_api.h")
            os.makedirs(os.path.dirname(header))
            with open(header, "w") as stream:
                stream.write("/* no number here */\n")
            run("add", ".")
            run("commit", "-m", "numberless base")
            run("push", "origin", "HEAD:base")
            with open(header, "w") as stream:
                stream.write("#define RADICLE_ABI_VERSION 92u\n")
            run("commit", "-am", "numbered head")
            here = os.getcwd()
            os.chdir(work)
            try:
                self.assertEqual(2, check.main(["--base", "base", "--local-only"]))
            finally:
                os.chdir(here)


class StackRootTest(unittest.TestCase):
    def test_a_three_link_walk_ends_on_the_integration_branch(self):
        claims = [claim(1041, 95, head_ref="branch1041", base_ref="nomad/dev"),
                  claim(1042, 95, head_ref="branch1042", base_ref="branch1041"),
                  claim(1043, 95, head_ref="branch1043", base_ref="branch1042")]
        self.assertEqual("nomad/dev", check.stack_root(claims, "branch1043"))

    def test_starting_on_the_integration_branch_is_no_stack(self):
        self.assertIsNone(check.stack_root([claim(848, 92, head_ref="nomad/dev")], "nomad/dev"))

    def test_an_unlinked_base_is_no_stack(self):
        self.assertIsNone(check.stack_root([claim(848, 92, head_ref="nomad/dev")], "feature-x"))

    def test_two_claims_on_one_branch_is_an_error(self):
        claims = [claim(1043, 95, head_ref="branch1043"), claim(1050, 95, head_ref="branch1043")]
        with self.assertRaises(ValueError):
            check.stack_root(claims, "branch1043")

    def test_a_loop_is_an_error(self):
        claims = [claim(1, 95, head_ref="x", base_ref="y"),
                  claim(2, 95, head_ref="y", base_ref="x")]
        with self.assertRaises(ValueError):
            check.stack_root(claims, "x")

    def test_read_version_at_reads_a_ref(self):
        body = {"content": base64.b64encode(b"#define RADICLE_ABI_VERSION 92u\n").decode()}
        real = check.get_json
        check.get_json = lambda url, token: body
        try:
            self.assertEqual(92, check.read_version_at("a", "r", "t", "nomad/dev", "root"))
        finally:
            check.get_json = real


class ClaimReadErrorsTest(unittest.TestCase):
    def numbered(self, version):
        return {"content": base64.b64encode(
            "#define RADICLE_ABI_VERSION {}u\n".format(version).encode()).decode()}

    def main_with(self, error, pulls=None, bodies=None, shared=95):
        if pulls is None:
            pulls = [{"number": 2, "head": {"sha": "abc", "ref": "other"},
                      "base": {"sha": "def", "ref": "nomad/dev"}}]
        real_pulls, real_json = check.open_pulls, check.get_json
        check.open_pulls = lambda api, repo, token: pulls

        def fake_json(url, token):
            if "/contents/" in url:
                ref = url.rsplit("ref=", 1)[1]
                if ref == "nomad/dev":
                    return self.numbered(shared)
                if bodies is not None and ref in bodies:
                    item = bodies[ref]
                    if isinstance(item, Exception):
                        raise item
                    return item
            raise error

        check.get_json = fake_json
        try:
            with tempfile.TemporaryDirectory() as top:
                origin = os.path.join(top, "origin")
                subprocess.run(("git", "init", "--bare", "origin"), check=True,
                               capture_output=True, text=True, cwd=top)
                root = os.path.join(top, "work")
                os.makedirs(root)
                run = lambda *args: subprocess.run(
                    ("git",) + args, check=True, capture_output=True, text=True, cwd=root)
                run("init", "-b", "main")
                run("config", "core.hooksPath", "")
                run("config", "user.email", "t@t")
                run("config", "user.name", "t")
                run("remote", "add", "origin", origin)
                header = os.path.join(root, "Radicle", "include", "fo4", "c_api.h")
                os.makedirs(os.path.dirname(header))
                with open(header, "w") as stream:
                    stream.write("#define RADICLE_ABI_VERSION 95u\n")
                run("add", ".")
                run("commit", "-m", "numbered")
                run("push", "origin", "HEAD:main")
                here = os.getcwd()
                os.chdir(root)
                try:
                    return check.main(["--base", "main", "--api-url", "a",
                                       "--repo", "r", "--token", "t", "--number", "1"])
                finally:
                    os.chdir(here)
        finally:
            check.open_pulls, check.get_json = real_pulls, real_json

    def test_a_gone_claim_is_skipped(self):
        error = urllib.error.HTTPError("u", 404, "Not Found", {}, None)
        self.assertEqual(0, self.main_with(error))

    def test_a_broken_claim_server_fails_the_gate(self):
        error = urllib.error.HTTPError("u", 500, "Broken", {}, None)
        self.assertEqual(2, self.main_with(error))

    def test_a_non_json_body_fails_the_gate(self):
        error = json.JSONDecodeError("Expecting value", "", 0)
        self.assertEqual(2, self.main_with(error))

    def test_a_numberless_header_is_skipped(self):
        pulls = [{"number": 300, "head": {"sha": "n300", "ref": "other"},
                  "base": {"sha": "def", "ref": "nomad/dev"}}]
        bodies = {"n300": {"content": base64.b64encode(b"/* empty */\n").decode()}}
        self.assertEqual(0, self.main_with(None, pulls, bodies))

    def main_with_list_body(self, page_one):
        real_json = check.get_json

        def fake_json(url, token):
            if "/pulls?" in url:
                if "page=1" in url:
                    return page_one
                return []
            if "/contents/" in url:
                return self.numbered(95)
            raise AssertionError("unexpected " + url)

        check.get_json = fake_json
        try:
            with tempfile.TemporaryDirectory() as top:
                origin = os.path.join(top, "origin")
                subprocess.run(("git", "init", "--bare", "origin"), check=True,
                               capture_output=True, text=True, cwd=top)
                root = os.path.join(top, "work")
                os.makedirs(root)
                run = lambda *args: subprocess.run(
                    ("git",) + args, check=True, capture_output=True, text=True, cwd=root)
                run("init", "-b", "main")
                run("config", "core.hooksPath", "")
                run("config", "user.email", "t@t")
                run("config", "user.name", "t")
                run("remote", "add", "origin", origin)
                header = os.path.join(root, "Radicle", "include", "fo4", "c_api.h")
                os.makedirs(os.path.dirname(header))
                with open(header, "w") as stream:
                    stream.write("#define RADICLE_ABI_VERSION 95u\n")
                run("add", ".")
                run("commit", "-m", "numbered")
                run("push", "origin", "HEAD:main")
                here = os.getcwd()
                os.chdir(root)
                try:
                    stream = io.StringIO()
                    with contextlib.redirect_stdout(stream):
                        code = check.main(["--base", "main", "--api-url", "a",
                                           "--repo", "r", "--token", "t", "--number", "1"])
                    self.assertIn("page 1", stream.getvalue())
                    return code
                finally:
                    os.chdir(here)
        finally:
            check.get_json = real_json

    def test_a_null_list_page_fails_the_gate(self):
        self.assertEqual(2, self.main_with_list_body(None))

    def test_an_error_object_list_page_fails_the_gate(self):
        self.assertEqual(2, self.main_with_list_body({"message": "oops"}))

    def test_a_list_of_strings_fails_the_gate(self):
        self.assertEqual(2, self.main_with_list_body(["x"]))

    def test_invalid_utf8_body_fails_the_gate(self):
        bodies = {"bad": UnicodeDecodeError("utf-8", b"\xff", 0, 1, "invalid start byte")}
        pulls = [{"number": 400, "head": {"sha": "bad", "ref": "other"},
                  "base": {"sha": "def", "ref": "nomad/dev"}}]
        self.assertEqual(2, self.main_with(None, pulls, bodies))

    def test_non_ascii_content_fails_the_gate(self):
        bodies = {"nas": {"content": "caf\xe9"}}
        pulls = [{"number": 401, "head": {"sha": "nas", "ref": "other"},
                  "base": {"sha": "def", "ref": "nomad/dev"}}]
        self.assertEqual(2, self.main_with(None, pulls, bodies))

    def test_a_null_base_fails_the_gate(self):
        pulls = [{"number": 402, "head": {"sha": "h402", "ref": "other"},
                  "base": None}]
        stream = io.StringIO()
        with contextlib.redirect_stdout(stream):
            code = self.main_with(AssertionError("must not fetch"), pulls)
        self.assertEqual(2, code)
        self.assertIn("malformed links", stream.getvalue())

    def test_a_list_head_ref_fails_the_gate(self):
        pulls = [{"number": 403, "head": {"sha": "h403", "ref": ["x"]},
                  "base": {"sha": "def", "ref": "nomad/dev"}}]
        self.assertEqual(2, self.main_with(AssertionError("must not fetch"), pulls))


class OpenPullsTest(unittest.TestCase):
    def test_short_pages_do_not_stop_the_list(self):
        seen = []

        def fake(url, token):
            seen.append(url)
            if "page=1" in url:
                return [{"number": 1}]
            if "page=2" in url:
                return [{"number": 2}, {"number": 3}]
            return []

        real = check.get_json
        check.get_json = fake
        try:
            pulls = check.open_pulls("a", "r", "t")
        finally:
            check.get_json = real
        self.assertEqual([{"number": 1}, {"number": 2}, {"number": 3}], pulls)
        self.assertTrue(any("page=3" in url for url in seen))

    def test_running_out_of_pages_is_an_error(self):
        real = check.get_json
        check.get_json = lambda url, token: [{"number": 1}]
        try:
            with self.assertRaises(ValueError):
                check.open_pulls("a", "r", "t")
        finally:
            check.get_json = real


class StackMainTest(unittest.TestCase):
    def body(self, version):
        return {"content": base64.b64encode(
            "#define RADICLE_ABI_VERSION {}u\n".format(version).encode()).decode()}

    def make_pr_repo(self, top, base_branch, base_header, head_header):
        origin = os.path.join(top, "origin")
        subprocess.run(("git", "init", "--bare", "origin"), check=True,
                       capture_output=True, text=True, cwd=top)
        work = os.path.join(top, "work")
        os.makedirs(work)
        run = lambda *args: subprocess.run(
            ("git",) + args, check=True, capture_output=True, text=True, cwd=work)
        run("init", "-b", "main")
        run("config", "core.hooksPath", "")
        run("config", "user.email", "t@t")
        run("config", "user.name", "t")
        run("remote", "add", "origin", origin)
        header = os.path.join(work, "Radicle", "include", "fo4", "c_api.h")
        nif = os.path.join(work, "Radicle", "include", "fo4", "c_api_nif.h")
        os.makedirs(os.path.dirname(header))
        with open(header, "w") as stream:
            stream.write("#define RADICLE_ABI_VERSION {}u\n".format(base_header))
        with open(nif, "w") as stream:
            stream.write("/* base */\n")
        run("add", ".")
        run("commit", "-m", "base")
        run("push", "origin", "HEAD:{}".format(base_branch))
        with open(header, "w") as stream:
            stream.write("#define RADICLE_ABI_VERSION {}u\n".format(head_header))
        with open(nif, "w") as stream:
            stream.write("/* head edit */\n")
        run("commit", "-am", "head")
        return work

    def raw(self, number, version, head_sha, head_ref, base_sha, base_ref):
        return {"number": number,
                "head": {"sha": head_sha, "ref": head_ref},
                "base": {"sha": base_sha, "ref": base_ref},
                "version": version}

    def run_main(self, top, base_branch, base_header, head_header, pulls, versions,
                 root_body, number, order=None, roots=None):
        work = self.make_pr_repo(top, base_branch, base_header, head_header)
        real_pulls, real_json = check.open_pulls, check.get_json
        check.open_pulls = lambda api, repo, token: pulls

        def fake_json(url, token):
            if "/compare/" in url:
                if order is None:
                    return {"total_commits": 1}
                seg = url.split("/compare/")[1]
                old, new = seg.split("...")
                pos = lambda s: order.get(s, 999)
                return {"total_commits": 1 if pos(new) > pos(old) else 0}
            if "/contents/" in url:
                ref = url.rsplit("ref=", 1)[1]
                if ref == "nomad/dev":
                    if isinstance(root_body, Exception):
                        raise root_body
                    return root_body
                if roots is not None and ref in roots:
                    return roots[ref]
                return self.body(versions[ref])
            raise AssertionError("unexpected " + url)

        check.get_json = fake_json
        here = os.getcwd()
        os.chdir(work)
        try:
            return check.main(["--base", base_branch, "--api-url", "a",
                               "--repo", "r", "--token", "t", "--number", str(number)])
        finally:
            os.chdir(here)
            check.open_pulls, check.get_json = real_pulls, real_json

    def test_a_tracking_branch_grants_no_exemption(self):
        pulls = [self.raw(848, 92, "s848", "nomad/dev", "smain", "main")]
        with tempfile.TemporaryDirectory() as top:
            self.assertEqual(1, self.run_main(top, "nomad/dev", 92, 92, pulls,
                                              {"s848": 92}, self.body(40), 100))

    def test_a_three_link_stack_may_keep_its_number(self):
        pulls = [self.raw(1041, 95, "s41", "branch1041", "sdev", "nomad/dev"),
                 self.raw(1042, 95, "s42", "branch1042", "s41", "branch1041"),
                 self.raw(1043, 95, "s43", "branch1043", "s42", "branch1042")]
        with tempfile.TemporaryDirectory() as top:
            self.assertEqual(0, self.run_main(top, "branch1043", 95, 95, pulls,
                                              {"s41": 95, "s42": 95, "s43": 95},
                                              self.body(92), 1044))

    def test_a_stack_at_the_root_number_must_bump(self):
        pulls = [self.raw(1041, 92, "s41", "branch1041", "sdev", "nomad/dev"),
                 self.raw(1042, 92, "s42", "branch1042", "s41", "branch1041"),
                 self.raw(1043, 92, "s43", "branch1043", "s42", "branch1042")]
        with tempfile.TemporaryDirectory() as top:
            self.assertEqual(1, self.run_main(top, "branch1043", 92, 92, pulls,
                                              {"s41": 92, "s42": 92, "s43": 92},
                                              self.body(92), 1044))

    def test_an_unreadable_root_fails_the_gate(self):
        pulls = [self.raw(1041, 95, "s41", "branch1041", "sdev", "nomad/dev"),
                 self.raw(1042, 95, "s42", "branch1042", "s41", "branch1041"),
                 self.raw(1043, 95, "s43", "branch1043", "s42", "branch1042")]
        with tempfile.TemporaryDirectory() as top:
            self.assertEqual(2, self.run_main(top, "branch1043", 95, 95, pulls,
                                              {"s41": 95, "s42": 95, "s43": 95},
                                              urllib.error.HTTPError("u", 500, "Broken", {}, None),
                                              1044))

    def test_an_ambiguous_link_fails_the_gate(self):
        pulls = [self.raw(1043, 95, "s43", "branch1043", "s42", "branch1042"),
                 self.raw(1050, 95, "s50", "branch1043", "s42", "branch1042")]
        with tempfile.TemporaryDirectory() as top:
            self.assertEqual(2, self.run_main(top, "branch1043", 95, 95, pulls,
                                              {"s43": 95, "s50": 95},
                                              self.body(92), 1044))

    def test_shared_number_skips_another_pair_colliding_on_it(self):
        pulls = [self.raw(848, 92, "s848", "nomad/dev", "smain", "main"),
                 self.raw(1039, 92, "s39", "br39", "sdev", "nomad/dev"),
                 self.raw(1040, 92, "s40", "br40", "sdev", "nomad/dev")]
        versions = {"s848": 92, "s39": 92, "s40": 92}
        with tempfile.TemporaryDirectory() as top:
            self.assertEqual(0, self.run_main(top, "main", 40, 92, pulls,
                                              versions, self.body(92), 848))

    def test_a_stack_child_beside_another_pair_keeps_its_number(self):
        pulls = [self.raw(1041, 95, "s41", "branch1041", "sdev", "nomad/dev"),
                 self.raw(1042, 95, "s42", "branch1042", "s41", "branch1041"),
                 self.raw(1039, 92, "s39", "br39", "sdev", "nomad/dev"),
                 self.raw(1040, 92, "s40", "br40", "sdev", "nomad/dev")]
        versions = {"s41": 95, "s42": 95, "s39": 92, "s40": 92}
        with tempfile.TemporaryDirectory() as top:
            self.assertEqual(0, self.run_main(top, "branch1041", 95, 95, pulls,
                                              versions, self.body(92), 1042))

    def test_an_unrelated_pair_does_not_fail_a_third_pr(self):
        pulls = [self.raw(200, 96, "s200", "br200", "sdev", "nomad/dev"),
                 self.raw(201, 96, "s201", "br201", "sdev", "nomad/dev")]
        with tempfile.TemporaryDirectory() as top:
            self.assertEqual(0, self.run_main(top, "nomad/dev", 95, 97, pulls,
                                              {"s200": 96, "s201": 96},
                                              self.body(95), 100))

    def test_each_pair_member_fails_on_its_own_run(self):
        for number in (200, 201):
            pulls = [self.raw(200, 96, "s200", "br200", "sdev", "nomad/dev"),
                     self.raw(201, 96, "s201", "br201", "sdev", "nomad/dev")]
            with tempfile.TemporaryDirectory() as top:
                self.assertEqual(1, self.run_main(top, "nomad/dev", 95, 96, pulls,
                                                  {"s200": 96, "s201": 96},
                                                  self.body(95), number))

    def test_a_two_link_stack_on_an_unheld_base(self):
        pulls = [self.raw(201, 96, "s201", "branch201", "sfx", "feature-x"),
                 self.raw(202, 96, "s202", "branch202", "s201", "branch201")]
        versions = {"s201": 96, "s202": 96}
        order = {"s201": 0, "s202": 1}
        with tempfile.TemporaryDirectory() as top:
            self.assertEqual(0, self.run_main(top, "branch202", 96, 96, pulls,
                                              versions, self.body(92),
                                              203, order, {"feature-x": self.body(95)}))
        with tempfile.TemporaryDirectory() as top:
            self.assertEqual(1, self.run_main(top, "branch202", 96, 96, pulls,
                                              versions, self.body(92),
                                              203, order, {"feature-x": self.body(96)}))


if __name__ == "__main__":
    unittest.main()
