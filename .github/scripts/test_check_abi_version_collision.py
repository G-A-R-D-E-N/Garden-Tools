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


if __name__ == "__main__":
    unittest.main()
