import os
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

import real_data_service as rds

SHA = "a" * 40
NEW = "b" * 40
CTEST_PASS = "  1/498 Test #1: CheckErrorsReal.Full ..........   Passed    1.00 sec\n100% tests passed, 0 tests failed out of 498\n"
CTEST_FAIL = ("  1/498 Test #1: CheckErrorsReal.Full ..........   Passed    1.00 sec\n"
              "  2/498 Test #2: QuestRuntimeReal.Thing ........***Failed    0.20 sec\n"
              "99% tests passed, 1 tests failed out of 498\n"
              "The following tests FAILED:\n"
              "\t2 - QuestRuntimeReal.Thing (Failed)\n")
REAL_CTEST = (" 86/501 Test #2355: Ba2RealData.ExtractsEveryVanillaEntry ....................***Skipped 0.03 sec\n"
              "121/501 Test #3720: CApiRuntimeReal.ReplaysTheSevenProgramsDeterministically ....................***Failed 0.11 sec\n"
              "501/501 Test  #119: RowRoundtripReal.CellRowsSurviveWriteAndReread .................... Passed 1404.62 sec\n"
              "99% tests passed, 1 tests failed out of 501\n"
              "Total Test time (real) = 1405.22 sec\n"
              "The following tests did not run:\n"
              "\t2355 - Ba2RealData.ExtractsEveryVanillaEntry (Skipped)\n"
              "The following tests FAILED:\n"
              "\t3720 - CApiRuntimeReal.ReplaysTheSevenProgramsDeterministically (Failed)\n"
              "Errors while running CTest\n")


def target(number=7, sha=SHA, base="nomad/dev", updated="2026-11-01T00:00:00Z", draft=False):
    return rds.Target(f"#{number}", sha, updated, base, number, draft=draft)


class FakeGitHub:
    def __init__(self, files=(), states=(), conclusions=(), head=SHA):
        self.files, self.states, self.conclusions, self.head = list(files), list(states), list(conclusions), head
        self.calls, self.posts = [], []

    def __call__(self, *args):
        self.calls.append(args)
        if "-f" in args:
            self.posts.append(dict(args[i + 1].split("=", 1) for i, arg in enumerate(args[:-1]) if arg == "-f"))
            return ""
        text = " ".join(args)
        if "/check-runs" in text:
            return "".join(conclusion + "\n" for conclusion in self.conclusions)
        if "/files" in text:
            return "\n".join(self.files) + "\n"
        if "/statuses" in text:
            return "".join(state + "\n" for state in self.states)
        if "/pulls/" in text or "/git/ref/" in text:
            return self.head + "\n"
        raise AssertionError(f"unexpected gh call: {text}")

    def states_posted(self):
        return [post["state"] for post in self.posts]


class FakeProc:
    def __init__(self, exit_code=None):
        self.exit_code = exit_code

    def wait(self, timeout=None):
        if self.exit_code is None:
            raise subprocess.TimeoutExpired("job", timeout)
        return self.exit_code

    def poll(self):
        return self.exit_code


class ServiceTest(unittest.TestCase):
    def setUp(self):
        tmp = Path(self.enterContext(tempfile.TemporaryDirectory()))
        self.enterContext(mock.patch.multiple(rds, ROOT=tmp, LOCK=tmp / "run.lock", LOG_DIR=tmp / "logs",
                                              REPO_DIR=tmp / "repo"))
        self.gh = FakeGitHub()
        self.enterContext(mock.patch.object(rds, "gh", self.gh))


class DueDecisionTest(ServiceTest):
    def test_scope_path_without_status_is_due(self):
        self.gh.files = ["Radicle/src/plugin/writer.cpp"]
        self.assertEqual(rds.plan(target()), "run")

    def test_posted_status_is_done_without_listing_files(self):
        self.gh.states = ["failure"]
        self.assertEqual(rds.plan(target()), "done")
        self.assertFalse(any("/files" in " ".join(call) for call in self.gh.calls))

    def test_pending_status_left_by_a_dead_run_is_due_again(self):
        self.gh.states = ["pending"]
        self.gh.files = ["PluginEditTool/native/src/a.cpp"]
        self.assertEqual(rds.plan(target()), "run")

    def test_untouched_paths_are_not_affected(self):
        self.gh.files = ["BGS/tools/x.cpp", "docs/CMakeLists.txt", "Radiclexx/a.cpp"]
        self.assertEqual(rds.plan(target()), "not affected")

    def test_root_cmakelists_matches_exactly_and_workflow_file_is_in_scope(self):
        self.gh.files = ["CMakeLists.txt"]
        self.assertEqual(rds.plan(target()), "run")
        self.gh.files = [".github/workflows/native-real-data.yml"]
        self.assertEqual(rds.plan(target()), "run")


class DocsExclusionTest(ServiceTest):
    def test_docs_and_markdown_are_not_in_scope(self):
        self.assertFalse(rds.in_scope("Radicle/docs/FO4_PARITY.txt"))
        self.assertFalse(rds.in_scope("Radicle/src/notes.md"))
        self.assertFalse(rds.in_scope("PluginEditTool/native/README.md"))

    def test_source_under_the_same_roots_is_still_in_scope(self):
        self.assertTrue(rds.in_scope("Radicle/src/plugin/writer.cpp"))
        self.assertTrue(rds.in_scope("PluginEditTool/native/src/a.cpp"))

    def test_docs_only_pr_is_not_affected(self):
        self.gh.files = ["Radicle/docs/FO4_PARITY.txt", "README.md"]
        self.assertEqual(rds.plan(target()), "not affected")


class EarlierPassTest(ServiceTest):
    def test_successful_check_run_counts_without_listing_files_or_building(self):
        self.gh.conclusions = ["success"]
        self.gh.files = ["Radicle/src/a.cpp"]
        self.assertEqual(rds.plan(target()), "earlier")
        self.assertFalse(any("/files" in " ".join(call) for call in self.gh.calls))

    def test_failed_cancelled_or_missing_check_run_does_not_count(self):
        for conclusions in (["failure"], ["cancelled", "failure"], []):
            with self.subTest(conclusions=conclusions):
                self.gh.conclusions = conclusions
                self.gh.files = ["Radicle/src/a.cpp"]
                self.assertEqual(rds.plan(target()), "run")

    def test_run_pass_posts_earlier_success_and_builds_nothing(self):
        self.gh.conclusions = ["success"]
        with mock.patch.object(rds, "open_targets", return_value=[target()]), \
                mock.patch.object(rds, "run_target") as run_target:
            rds.run_pass()
        run_target.assert_not_called()
        self.assertEqual(self.gh.posts, [{"state": "success", "context": "real-data",
                                          "description": "passed on the earlier runner"}])


class DraftTest(ServiceTest):
    def test_draft_is_skipped_without_any_github_call(self):
        self.assertEqual(rds.plan(target(draft=True)), "draft")
        self.assertEqual(self.gh.calls, [])

    def test_run_pass_leaves_drafts_unstatused(self):
        with mock.patch.object(rds, "open_targets", return_value=[target(draft=True)]), \
                mock.patch.object(rds, "run_target") as run_target:
            counts = rds.run_pass()
        run_target.assert_not_called()
        self.assertEqual(counts["draft"], 1)
        self.assertEqual(self.gh.posts, [])


class OrderTest(unittest.TestCase):
    def test_dev_head_first_then_newest_updated_pr(self):
        targets = [target(1, updated="2026-11-01T01:00:00Z"), target(2, updated="2026-11-01T05:00:00Z"),
                   rds.Target(rds.DEV_BRANCH, SHA, "2026-10-01T00:00:00Z", "")]
        self.assertEqual([t.name for t in rds.order(targets)], [rds.DEV_BRANCH, "#2", "#1"])


class StatusTextTest(ServiceTest):
    def test_failure_text_has_no_path_or_username(self):
        log = CTEST_FAIL.replace("QuestRuntimeReal.Thing", r"Ricky.Thing C:\Users\Ricky\build\x.exe")
        with mock.patch.dict(os.environ, {"USERNAME": "Ricky"}):
            state, text = rds.outcome(log, 1)
            rds.post(SHA, state, text)
        description = self.gh.posts[-1]["description"]
        self.assertEqual(state, "failure")
        self.assertNotRegex(description, r"[\\/]")
        self.assertNotIn("Users", description)
        self.assertNotIn("ricky", description.lower())
        self.assertLessEqual(len(description), rds.DESC_MAX)

    def test_counts_and_first_failing_name(self):
        self.assertEqual(rds.outcome(CTEST_FAIL, 1), ("failure", "498 tests, 1 failed: QuestRuntimeReal.Thing"))

    def test_success_text(self):
        self.assertEqual(rds.outcome(CTEST_PASS, 0), ("success", "498 tests, 0 failed"))

    def test_no_summary_means_build_failed_with_no_counts(self):
        self.assertEqual(rds.outcome("error C2065\n", 1), ("failure", "no test summary (exit 1), 0 tests run"))


class RealCtestOutputTest(unittest.TestCase):
    def test_summary_and_failed_name_from_a_real_run(self):
        self.assertEqual(rds.outcome(REAL_CTEST, 1),
                         ("failure", "501 tests, 1 failed: CApiRuntimeReal.ReplaysTheSevenProgramsDeterministically"))

    def test_skipped_tests_are_never_reported_as_the_failure(self):
        self.assertEqual(rds.failed_names(REAL_CTEST), ["CApiRuntimeReal.ReplaysTheSevenProgramsDeterministically"])


class WorkflowFileTest(ServiceTest):
    def test_checkout_workflow_is_used_when_the_commit_has_it(self):
        path = rds.REPO_DIR / rds.WORKFLOW
        path.parent.mkdir(parents=True)
        path.write_text("name: checkout\n", encoding="utf-8")
        with mock.patch.object(rds, "git", side_effect=AssertionError("no git needed")):
            self.assertEqual(rds.workflow_file(), path)

    def test_dev_copy_is_used_when_the_commit_lacks_the_workflow(self):
        rds.REPO_DIR.mkdir(parents=True)
        calls = []

        def fake_git(*args, cwd=None):
            calls.append(args)
            return "name: dev copy\n" if args[:1] == ("show",) else ""

        with mock.patch.object(rds, "git", side_effect=fake_git):
            path = rds.workflow_file()
        self.assertEqual(path.read_text(encoding="utf-8"), "name: dev copy\n")
        self.assertIn(("fetch", "--no-tags", "origin", "+refs/heads/nomad/dev:refs/real-data/dev"), calls)
        self.assertTrue(path.is_relative_to(rds.ROOT))


class PublicSyncTest(ServiceTest):
    def test_sync_is_requested_on_the_public_repo(self):
        with mock.patch.object(rds, "gh") as gh:
            rds.request_public_sync()
        gh.assert_called_once_with("workflow", "run", "sync-from-private.yml", "--repo", "G-A-R-D-E-N/Garden-Tools",
                                   "--ref", "main")

    # Detects: a GitHub outage ending the sync thread, so private pushes wait hours for a delayed schedule.
    def test_a_failed_request_does_not_stop_the_loop(self):
        error = subprocess.CalledProcessError(1, "gh")
        with mock.patch.object(rds, "gh", side_effect=error):
            rds.request_public_sync()


class CheckoutTest(ServiceTest):
    def git_calls(self, parents):
        calls = []

        def fake_git(*args, cwd=None):
            calls.append(args)
            return f"{'c' * 40} {parents}\n" if args[:1] == ("rev-list",) else ""

        return calls, mock.patch.object(rds, "git", side_effect=fake_git)

    def test_merge_ref_whose_second_parent_is_an_older_head_is_left_due(self):
        calls, patcher = self.git_calls(f"{'d' * 40} {'e' * 40}")
        with patcher:
            self.assertFalse(rds.prepare_checkout(target()))
        self.assertFalse(any(call[:1] == ("checkout",) for call in calls))

    def test_current_merge_ref_is_checked_out_after_a_clean_that_keeps_build(self):
        calls, patcher = self.git_calls(f"{'d' * 40} {SHA}")
        with patcher:
            self.assertTrue(rds.prepare_checkout(target()))
        self.assertIn(("clean", "-ffdx", "-e", "/build/"), calls)
        self.assertIn(("checkout", "-f", "--detach", "refs/real-data/candidate"), calls)

    # Detects: every run after the first stopping in 12 seconds because zlib was copied inside its old folder.
    def test_zlib_copy_from_the_last_run_is_removed_and_the_build_tree_is_kept(self):
        build = rds.REPO_DIR / "build"
        (build / "zlib-src").mkdir(parents=True)
        pack = build / "zlib-src" / "pack.idx"
        pack.write_text("old", encoding="utf-8")
        pack.chmod(0o444)
        (build / "native").mkdir()
        (rds.REPO_DIR / ".git").mkdir()
        _, patcher = self.git_calls(f"{'d' * 40} {SHA}")
        with patcher:
            self.assertTrue(rds.prepare_checkout(target()))
        self.assertFalse((build / "zlib-src").exists())
        self.assertTrue((build / "native").exists())


class GhPathTest(ServiceTest):
    def test_gh_path_given_at_install_is_used_for_the_run(self):
        self.enterContext(mock.patch.object(rds, "GH", "gh"))
        with mock.patch.object(rds, "run_pass") as run_pass:
            rds.main(["--dry-run", "--gh", r"C:\Program Files\GitHub CLI\gh.exe"])
        run_pass.assert_called_once()
        self.assertEqual(rds.GH, r"C:\Program Files\GitHub CLI\gh.exe")


class RunTargetTest(ServiceTest):
    def run_with(self, proc, head=SHA, timeout=3600, poll=0):
        kill = mock.Mock(side_effect=lambda p: setattr(p, "exit_code", 1))
        with mock.patch.object(rds, "prepare_checkout", return_value=True), \
                mock.patch.object(rds, "workflow_file", return_value=rds.REPO_DIR / rds.WORKFLOW), \
                mock.patch.object(rds, "start_job", return_value=proc), \
                mock.patch.object(rds, "kill_tree", kill), \
                mock.patch.object(rds, "TIMEOUT", timeout), mock.patch.object(rds, "POLL", poll), \
                mock.patch.object(rds, "current_head", return_value=head):
            rds.run_target(target())
        return kill

    def test_newer_head_stops_job_and_posts_only_superseded(self):
        kill = self.run_with(FakeProc(), head=NEW)
        self.assertEqual(self.gh.states_posted(), ["pending", "error"])
        self.assertEqual(self.gh.posts[-1]["description"], "superseded")
        kill.assert_called_once()

    def test_head_moving_before_exit_posts_superseded_not_the_result(self):
        kill = self.run_with(FakeProc(exit_code=0), head=NEW, timeout=3600)
        self.assertEqual(self.gh.states_posted(), ["pending", "error"])
        self.assertEqual(self.gh.posts[-1]["description"], "superseded")
        kill.assert_not_called()

    def test_job_past_the_run_timeout_is_killed_and_posts_timed_out(self):
        kill = self.run_with(FakeProc(), timeout=-1)
        self.assertEqual(self.gh.states_posted(), ["pending", "error"])
        self.assertEqual(self.gh.posts[-1]["description"], "timed out")
        kill.assert_called_once()

    def test_final_result_is_read_from_the_local_log(self):
        rds.LOG_DIR.mkdir(parents=True)
        (rds.LOG_DIR / f"{SHA}.log").write_text(CTEST_FAIL, encoding="utf-8")
        self.run_with(FakeProc(exit_code=1))
        self.assertEqual(self.gh.states_posted(), ["pending", "failure"])
        self.assertEqual(self.gh.posts[-1]["description"], "498 tests, 1 failed: QuestRuntimeReal.Thing")


class NotAffectedTest(ServiceTest):
    def test_untouched_target_gets_success_without_running_a_job(self):
        self.gh.files = ["BGS/tools/x.cpp"]
        with mock.patch.object(rds, "open_targets", return_value=[target()]), \
                mock.patch.object(rds, "run_target") as run_target:
            counts = rds.run_pass()
        run_target.assert_not_called()
        self.assertEqual(counts["not affected"], 1)
        self.assertEqual(self.gh.posts, [{"state": "success", "context": "real-data", "description": "not affected"}])


class LockTest(ServiceTest):
    def dead_pid(self):
        proc = subprocess.Popen([sys.executable, "-I", "-B", "-c", "pass"])
        proc.wait()
        return proc.pid

    def write_lock(self, pid, started):
        rds.LOCK.write_text(f'{{"pid": {pid}, "started": {started}}}', encoding="utf-8")

    def test_stale_lock_from_a_dead_process_is_taken_over(self):
        self.write_lock(self.dead_pid(), time.time())
        self.assertTrue(rds.acquire_lock())

    def test_fresh_lock_held_by_a_live_process_blocks(self):
        self.write_lock(os.getpid(), time.time())
        self.assertFalse(rds.acquire_lock())

    def test_lock_older_than_the_limit_is_stale_even_when_the_pid_lives(self):
        self.write_lock(os.getpid(), time.time() - rds.LOCK_MAX_AGE - 1)
        self.assertTrue(rds.acquire_lock())


class OneAtATimeTest(ServiceTest):
    def test_second_acquire_fails_until_release(self):
        self.assertTrue(rds.acquire_lock())
        self.assertFalse(rds.acquire_lock())
        rds.release_lock()
        self.assertTrue(rds.acquire_lock())

    def test_pass_does_nothing_while_another_run_holds_the_lock(self):
        rds.LOCK.write_text(f'{{"pid": {os.getpid()}, "started": {time.time()}}}', encoding="utf-8")
        with mock.patch.object(rds, "open_targets") as open_targets, \
                mock.patch.object(rds, "run_target") as run_target:
            rds.run_pass()
        open_targets.assert_not_called()
        run_target.assert_not_called()
        self.assertEqual(self.gh.posts, [])


if __name__ == "__main__":
    unittest.main()
