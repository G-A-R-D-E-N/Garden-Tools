import base64
import io
import json
import os
import runpy
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

import real_data_service as rds

SHA = "a" * 40
NEW = "b" * 40
OLD = "f" * 40
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
CALL_FAILED = subprocess.CalledProcessError(1, "gh", stderr="HTTP 502\n")


def target(number=7, sha=SHA, base="nomad/dev", updated="2026-11-01T00:00:00Z", draft=False):
    return rds.Target(f"#{number}", sha, updated, base, number, draft=draft)


class FakeGitHub:
    def __init__(self, files=(), states=(), conclusions=(), head=SHA):
        self.files, self.states, self.conclusions, self.head = list(files), list(states), list(conclusions), head
        self.calls, self.posts = [], []

    def __call__(self, *args, **kwargs):
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
                                              REPO_DIR=tmp / "repo", QUEUED=tmp / "queued.json"))
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

    def test_superseded_head_that_is_back_in_the_pr_is_due_again(self):
        self.gh.states = ["error superseded"]
        self.gh.files = ["PluginEditTool/native/src/a.cpp"]
        self.assertEqual(rds.plan(target()), "run")

    def test_queued_status_does_not_count_as_done(self):
        self.gh.states = ["pending queued (position 1 of 2)"]
        self.gh.files = ["PluginEditTool/native/src/a.cpp"]
        self.assertEqual(rds.plan(target()), "run")

    # Detects: a head whose VM connection dropped, or whose VM fetch failed, treated as done so it never runs again.
    def test_unreachable_or_fetch_failed_head_is_due_again(self):
        self.gh.files = ["PluginEditTool/native/src/a.cpp"]
        for description in ("vm data runner unreachable", "vm data runner fetch failed"):
            with self.subTest(description=description):
                self.gh.states = [f"error {description}"]
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
    def test_dev_head_then_prs_into_dev_then_stacked_then_drafts_each_newest_first(self):
        targets = [target(1, updated="2026-11-01T01:00:00Z"), target(2, updated="2026-11-01T05:00:00Z"),
                   target(3, updated="2026-11-01T09:00:00Z", base="work/stack"),
                   target(4, updated="2026-11-01T07:00:00Z", draft=True),
                   rds.Target(rds.DEV_BRANCH, SHA, "2026-10-01T00:00:00Z", "")]
        self.assertEqual([t.name for t in rds.order(targets)], [rds.DEV_BRANCH, "#2", "#1", "#3", "#4"])


class QueueTest(ServiceTest):
    def test_queued_status_is_posted_only_when_the_position_changes(self):
        statuses = {SHA: ("pending", "queued (position 1 of 2)"), NEW: (None, "")}
        with mock.patch.object(rds, "latest_status", side_effect=statuses.__getitem__), \
                mock.patch.object(rds, "post") as post:
            rds.post_queue([target(1, sha=SHA), target(2, sha=NEW)], set())
        post.assert_called_once_with(NEW, "pending", "queued (position 2 of 2)")

    def test_tracked_heads_are_written_to_the_queue_file(self):
        with mock.patch.object(rds, "latest_status", return_value=(None, "")), mock.patch.object(rds, "post"):
            rds.post_queue([target(1, sha=SHA)], set())
        self.assertEqual(rds.load_queued(), {SHA})

    def test_queued_head_that_is_no_longer_due_is_marked_superseded(self):
        statuses = {OLD: ("pending", "queued (position 2 of 2)"), NEW: ("pending", "queued (position 1 of 1)")}
        queued = {OLD, NEW}
        with mock.patch.object(rds, "latest_status", side_effect=statuses.__getitem__), \
                mock.patch.object(rds, "post") as post:
            rds.clear_stale_queue({NEW}, queued)
        post.assert_called_once_with(OLD, "error", "superseded")
        self.assertEqual(queued, {NEW})
        self.assertEqual(rds.load_queued(), {NEW})

    def test_a_current_non_draft_head_is_never_marked_superseded(self):
        queued = {NEW}
        with mock.patch.object(rds, "latest_status", return_value=("pending", "queued (position 1 of 1)")), \
                mock.patch.object(rds, "post") as post:
            rds.clear_stale_queue({NEW}, queued)
        post.assert_not_called()
        self.assertEqual(queued, {NEW})

    def test_a_status_the_service_did_not_queue_is_never_touched(self):
        with mock.patch.object(rds, "latest_status", return_value=("pending", "queued (position 1 of 1)")), \
                mock.patch.object(rds, "post") as post:
            rds.clear_stale_queue(set(), set())
        post.assert_not_called()

    def test_a_tracked_head_whose_status_moved_on_is_forgotten_without_a_write(self):
        queued = {OLD}
        with mock.patch.object(rds, "latest_status", return_value=("error", "superseded")), \
                mock.patch.object(rds, "post") as post:
            rds.clear_stale_queue(set(), queued)
        post.assert_not_called()
        self.assertEqual(queued, set())

    def test_run_pass_clears_a_stale_queued_head_before_queueing_the_current_one(self):
        rds.QUEUED.write_text(json.dumps([OLD]), encoding="utf-8")
        self.gh.files = ["Radicle/src/a.cpp"]
        statuses = {OLD: ("pending", "queued (position 1 of 1)"), NEW: (None, "")}
        with mock.patch.object(rds, "open_targets", return_value=[target(2, sha=NEW)]), \
                mock.patch.object(rds, "latest_status", side_effect=statuses.__getitem__), \
                mock.patch.object(rds, "post") as post, \
                mock.patch.object(rds, "run_target"):
            rds.run_pass()
        self.assertEqual(post.call_args_list, [
            mock.call(OLD, "error", "superseded"),
            mock.call(NEW, "pending", "queued (position 1 of 1)"),
        ])

    def test_dry_run_neither_posts_nor_rewrites_the_queue_file(self):
        rds.QUEUED.write_text(json.dumps([OLD]), encoding="utf-8")
        self.enterContext(redirect_stdout(io.StringIO()))
        with mock.patch.object(rds, "open_targets", return_value=[target(2, sha=NEW)]), \
                mock.patch.object(rds, "latest_status", return_value=("pending", "queued (position 1 of 1)")), \
                mock.patch.object(rds, "post") as post:
            rds.run_pass(dry_run=True)
        post.assert_not_called()
        self.assertEqual(rds.load_queued(), {OLD})

    def test_run_pass_queues_every_due_head_and_refreshes_positions_after_each_job(self):
        self.gh.files = ["Radicle/src/a.cpp"]
        targets = [target(1, sha=SHA, updated="2026-11-01T01:00:00Z"),
                   target(2, sha=NEW, updated="2026-11-01T05:00:00Z")]
        with mock.patch.object(rds, "open_targets", return_value=targets), \
                mock.patch.object(rds, "latest_status", return_value=(None, "")), \
                mock.patch.object(rds, "post") as post, \
                mock.patch.object(rds, "run_target") as run_target:
            rds.run_pass()
        self.assertEqual(post.call_args_list, [
            mock.call(NEW, "pending", "queued (position 1 of 2)"),
            mock.call(SHA, "pending", "queued (position 2 of 2)"),
            mock.call(SHA, "pending", "queued (position 1 of 1)"),
        ])
        self.assertEqual(run_target.call_args_list, [mock.call(targets[1]), mock.call(targets[0])])


def dry_run_github(cmd, **kwargs):
    text = " ".join(cmd)
    if "pr list" in text:
        out = "[]"
    elif "/git/ref/" in text:
        out = SHA + "\n"
    elif "/compare/" in text or "/statuses" in text or "/check-runs" in text:
        out = ""
    else:
        out = json.dumps({"commit": {"committer": {"date": "2026-11-01T00:00:00Z"}}, "parents": [{"sha": NEW}]})
    return subprocess.CompletedProcess(cmd, 0, stdout=out, stderr="")


class MainExitTest(ServiceTest):
    def test_a_clean_dry_run_exit_is_not_logged_as_a_stopped_service(self):
        self.enterContext(mock.patch.object(sys, "argv", ["real_data_service.py", "--dry-run"]))
        self.enterContext(mock.patch.object(subprocess, "run", side_effect=dry_run_github))
        self.enterContext(mock.patch.object(Path, "write_text", side_effect=AssertionError("dry run wrote a file")))
        self.enterContext(redirect_stdout(io.StringIO()))
        with self.assertNoLogs(level="ERROR"), self.assertRaises(SystemExit) as exit_:
            runpy.run_path(str(Path(rds.__file__)), run_name="__main__")
        self.assertEqual(exit_.exception.code, 0)


class RetryTest(unittest.TestCase):
    def setUp(self):
        self.enterContext(mock.patch.object(rds, "RETRY_DELAYS", (0, 0)))
        self.run = self.enterContext(mock.patch.object(rds.subprocess, "run"))

    def test_a_failed_read_is_retried_and_its_stderr_is_logged(self):
        ok = subprocess.CompletedProcess([], 0, stdout="7\n", stderr="")
        self.run.side_effect = [CALL_FAILED, CALL_FAILED, ok]
        with self.assertLogs(level="WARNING") as logs:
            self.assertEqual(rds.gh("api", "repos/x"), "7\n")
        self.assertEqual(self.run.call_count, 3)
        self.assertIn("HTTP 502", logs.output[0])

    def test_a_read_gives_up_after_three_attempts(self):
        self.run.side_effect = CALL_FAILED
        with self.assertLogs(level="WARNING"), self.assertRaises(subprocess.CalledProcessError):
            rds.gh("api", "repos/x")
        self.assertEqual(self.run.call_count, 3)

    def test_a_status_post_is_tried_at_most_twice(self):
        self.run.side_effect = CALL_FAILED
        with self.assertLogs(level="WARNING"), self.assertRaises(subprocess.CalledProcessError):
            rds.post(SHA, "error", "superseded")
        self.assertEqual(self.run.call_count, 2)

    def test_public_sync_is_tried_once_and_its_warning_has_scrubbed_stderr(self):
        self.run.side_effect = subprocess.CalledProcessError(
            1, "gh", stderr="HTTP 403: Forbidden for Ricky (https://api.github.com/repos/x)\n")
        with mock.patch.dict(os.environ, {"USERNAME": "Ricky"}), self.assertLogs(level="WARNING") as logs:
            rds.request_public_sync()
        self.assertEqual(self.run.call_count, 1)
        self.assertIn("public CI sync not started: HTTP 403: Forbidden for", logs.output[-1])
        self.assertNotIn("Ricky", logs.output[-1])
        self.assertNotIn("https", logs.output[-1])


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

        def fake_git(*args, **kwargs):
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
                                   "--ref", "main", attempts=1)

    # Detects: a GitHub outage ending the sync thread, so private pushes wait hours for a delayed schedule.
    def test_a_failed_request_does_not_stop_the_loop(self):
        error = subprocess.CalledProcessError(1, "gh")
        with mock.patch.object(rds, "gh", side_effect=error):
            rds.request_public_sync()


class CheckoutTest(ServiceTest):
    def git_calls(self, parents):
        calls = []

        def fake_git(*args, **kwargs):
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


VM = rds.Worker("vm", ("ssh", "root@host", "ssh", "ciadmin@vm"), "C:/ci/real-data")


class WorkerConfigTest(ServiceTest):
    # Detects: a PC without workers.json starting a remote worker, which would change the single-runner service.
    def test_without_workers_json_only_the_local_worker_exists(self):
        self.assertEqual(rds.load_workers(), [])

    # Detects: a ~ path or Windows backslashes reaching ssh unchanged, so every VM run fails before it starts.
    def test_home_paths_are_expanded_and_the_root_uses_forward_slashes(self):
        config = [{"name": "vm", "ssh": ["ssh", "-i", "~/.ssh/key", "root@host"], "root": "C:\\ci\\real-data\\"}]
        (rds.ROOT / "workers.json").write_text(json.dumps(config), encoding="utf-8")
        with mock.patch.dict(os.environ, {"USERPROFILE": str(rds.ROOT), "HOME": str(rds.ROOT)}):
            (worker,) = rds.load_workers()
            self.assertEqual(worker.ssh[2], os.path.expanduser("~/.ssh/key"))
        self.assertNotIn("~", worker.ssh[2])
        self.assertEqual(worker.root, "C:/ci/real-data")


class RemoteCommandTest(ServiceTest):
    # Detects: a VM command or path built with backslashes, which nested ssh mangles, or a base branch dropped from it.
    def test_run_command_uses_forward_slashes_and_passes_the_base_only_for_a_pull_request(self):
        with mock.patch.object(rds.subprocess, "Popen") as popen:
            rds.start_remote(VM, target(), rds.LOG_DIR / "x.log", "vm.bundle")
            rds.start_remote(VM, rds.Target(rds.DEV_BRANCH, SHA, "2026-11-01T00:00:00Z", ""), rds.LOG_DIR / "x.log",
                             "vm.bundle")
        pull, push = (call.args[0][len(VM.ssh):] for call in popen.call_args_list)
        self.assertEqual(pull[pull.index("-File") + 1], "C:/ci/real-data/service/run_remote.ps1")
        self.assertEqual(pull[pull.index("-Bundle") + 1], "C:/ci/real-data/incoming/vm.bundle")
        self.assertEqual(pull[pull.index("-Root") + 1], "C:/ci/real-data")
        self.assertEqual(pull[pull.index("-BaseRef") + 1], "nomad/dev")
        self.assertEqual(push[push.index("-Event") + 1], "push")
        self.assertNotIn("-BaseRef", push)
        self.assertFalse(any("\\" in word for word in pull + push))

    # Detects: a bundle piped to a folder the VM never extracts to, or tar's output not reaching the ssh extract.
    def test_bundle_goes_over_tar_on_the_same_ssh_chain(self):
        tar = mock.Mock()
        tar.wait.return_value = 0
        with mock.patch.object(rds.subprocess, "Popen", return_value=tar) as popen, \
                mock.patch.object(rds.subprocess, "run", return_value=subprocess.CompletedProcess([], 0)) as run:
            rds.send_bundle(VM, rds.ROOT / "bundles" / "vm.bundle")
        self.assertEqual(popen.call_args.args[0], ["tar", "cf", "-", "-C", str(rds.ROOT / "bundles"), "vm.bundle"])
        self.assertEqual(run.call_args.args[0], [*VM.ssh, "tar", "xf", "-", "-C", "C:/ci/real-data/incoming"])
        self.assertIs(run.call_args.kwargs["stdin"], tar.stdout)


class RemoteTest(ServiceTest):
    def setUp(self):
        super().setUp()
        self.sent, self.has_workflow, self.git_calls, self.git_cwds = "", True, [], []
        self.enterContext(mock.patch.object(rds, "git", side_effect=self.fake_git))
        self.enterContext(mock.patch.object(rds, "fetch_candidate", return_value=True))
        self.enterContext(mock.patch.object(rds, "current_head", side_effect=lambda t: t.sha))
        self.enterContext(mock.patch.object(rds, "POLL", 0))
        self.send = self.enterContext(mock.patch.object(rds, "send_bundle"))
        self.run = self.enterContext(mock.patch.object(rds.subprocess, "run"))

    def fake_git(self, *args, **kwargs):
        self.git_calls.append(args)
        self.git_cwds.append(kwargs.get("cwd"))
        if args[:2] == ("update-ref", "-d"):
            self.sent = ""
        outputs = {"ls-tree": rds.WORKFLOW + "\n" if self.has_workflow else "", "for-each-ref": self.sent + "\n",
                   "rev-parse": NEW + "\n"}
        return outputs.get(args[0], "")

    def start_with(self, *exit_codes):
        codes = list(exit_codes)

        def start(worker, t, log_path, bundle_name):
            code = codes.pop(0)
            shell_code, mark = code if isinstance(code, tuple) else (code, None)
            log_path.parent.mkdir(parents=True, exist_ok=True)
            log_path.write_text(CTEST_PASS + (f"REAL_DATA_EXIT={mark}\n" if mark is not None else ""), encoding="utf-8")
            return FakeProc(shell_code)

        return self.enterContext(mock.patch.object(rds, "start_remote", side_effect=start))


class DeclineTest(RemoteTest):
    # Detects: a commit without the workflow, or a base name with shell characters, running on the VM.
    def test_commit_without_the_workflow_is_left_for_the_local_runner(self):
        self.has_workflow = False
        start = self.start_with()
        self.assertIs(rds.run_target_remote(target(), VM), False)
        start.assert_not_called()
        self.send.assert_not_called()
        self.assertEqual(self.gh.posts, [])

    def test_unsafe_base_name_is_left_for_the_local_runner(self):
        self.assertIs(rds.run_target_remote(target(base="main;calc"), VM), False)
        self.send.assert_not_called()

    # Detects: a head declined by the VM dropped from the queue, so the local runner never sees it.
    def test_declined_head_stays_queued_for_the_local_runner(self):
        self.has_workflow = False
        queued = set()
        rds.run_due([target()], queued, [VM])
        self.assertEqual(self.gh.posts[-1]["description"], "queued (position 1 of 1)")
        with mock.patch.object(rds, "run_target") as local:
            rds.run_due([target()], queued, [rds.Worker("local")])
        local.assert_called_once_with(target())


class WorkerPoolTest(RemoteTest):
    # Detects: the VM waiting for the local job, so two due heads never run at the same time.
    def test_local_and_vm_run_two_due_heads_at_once_and_each_posts_its_own_pending_text(self):
        barrier = threading.Barrier(2, timeout=5)

        def job(t, log_path, *_):
            barrier.wait()
            log_path.parent.mkdir(parents=True, exist_ok=True)
            log_path.write_text(CTEST_PASS, encoding="utf-8")
            return FakeProc(0)

        self.gh.files = ["Radicle/src/a.cpp"]
        config = [{"name": "vm", "ssh": ["ssh", "vm"], "root": "C:/ci/real-data"}]
        (rds.ROOT / "workers.json").write_text(json.dumps(config), encoding="utf-8")
        self.enterContext(mock.patch.object(rds, "open_targets", return_value=[target(7, sha=SHA), target(8, sha=NEW)]))
        self.enterContext(mock.patch.object(rds, "prepare_checkout", return_value=True))
        self.enterContext(mock.patch.object(rds, "workflow_file", return_value=rds.REPO_DIR / rds.WORKFLOW))
        self.enterContext(mock.patch.object(rds, "start_job", side_effect=job))
        self.enterContext(mock.patch.object(rds, "start_remote", side_effect=lambda w, t, log, name: job(t, log)))
        rds.run_pass()
        pending = sorted(post["description"] for post in self.gh.posts if post["description"].startswith("running on"))
        self.assertEqual(pending, ["running on the local data runner", "running on the vm data runner"])
        self.assertEqual(self.gh.states_posted().count("success"), 2)


class BundleTest(RemoteTest):
    # Detects: an incremental bundle against the commit the VM already has, which git refuses as empty.
    def test_bundle_is_incremental_only_against_an_older_commit_the_vm_has(self):
        for sent, prerequisites in ((OLD, [f"^{OLD}"]), ("", []), (NEW, [])):
            with self.subTest(sent=sent):
                self.sent, self.git_calls = sent, []
                self.start_with(0)
                rds.run_remote(target(), VM, rds.LOG_DIR / "x.log")
                (bundle,) = [call for call in self.git_calls if call[:2] == ("bundle", "create")]
                self.assertEqual([arg for arg in bundle if arg.startswith("^")], prerequisites)


    # Detects: bundles cut from the shallow local job repo, which the VM rejects as incomplete.
    def test_remote_git_runs_in_the_full_history_repo(self):
        self.start_with(0)
        rds.run_remote(target(), VM, rds.LOG_DIR / "x.log")
        self.assertTrue(self.git_cwds)
        self.assertEqual(set(self.git_cwds), {rds.REMOTE_DIR})


class RemoteStopTest(RemoteTest):
    # Detects: a superseded VM run that keeps building because only the local ssh client was killed.
    def test_superseded_vm_run_kills_the_remote_tree_through_run_pid(self):
        kill = self.enterContext(mock.patch.object(rds, "kill_tree",
                                                   side_effect=lambda proc: setattr(proc, "exit_code", 1)))
        self.start_with(None)
        self.enterContext(mock.patch.object(rds, "current_head", return_value=NEW))
        rds.run_target_remote(target(), VM)
        kill.assert_called_once()
        self.assertEqual(self.gh.posts[-1]["description"], "superseded")
        command = self.run.call_args.args[0]
        self.assertEqual(command[:len(VM.ssh)], list(VM.ssh))
        script = base64.b64decode(command[-1]).decode("utf-16-le")
        self.assertIn("taskkill /T /F /PID", script)
        self.assertIn("C:/ci/real-data/run.pid", script)


class FetchRetryTest(RemoteTest):
    # Detects: a VM that lost the previous commit failing the head outright, or retrying more than once.
    def test_exit_97_resends_full_history_once_and_then_runs(self):
        self.sent = OLD
        start = self.start_with(97, 0)
        rds.run_target_remote(target(), VM)
        self.assertEqual(start.call_count, 2)
        bundles = [[arg for arg in call if arg.startswith("^")] for call in self.git_calls
                   if call[:2] == ("bundle", "create")]
        self.assertEqual(bundles, [[f"^{OLD}"], []])
        self.assertIn(("update-ref", "-d", "refs/real-data/sent-vm"), self.git_calls)
        self.assertEqual(self.gh.states_posted(), ["pending", "success"])


    # Detects: the VM's PowerShell shell turning exit 97 into 1, so a lost prerequisite reads as a failed run.
    def test_exit_code_comes_from_the_marker_when_the_shell_flattens_it(self):
        self.sent = OLD
        start = self.start_with((1, 97), (1, 0))
        rds.run_target_remote(target(), VM)
        self.assertEqual(start.call_count, 2)
        self.assertEqual(self.gh.states_posted(), ["pending", "success"])


class UnreachableTest(RemoteTest):
    # Detects: a dropped VM connection posted as a failed test run, which would never be retried.
    def test_lost_connection_posts_unreachable_not_a_failure_and_backs_off(self):
        self.start_with(255)
        with mock.patch.object(rds.time, "sleep") as sleep, mock.patch.object(rds, "POLL", 1):
            rds.run_target_remote(target(), VM)
        self.assertEqual(self.gh.states_posted(), ["pending", "error"])
        self.assertEqual(self.gh.posts[-1]["description"], "vm data runner unreachable")
        sleep.assert_called_once_with(5)


if __name__ == "__main__":
    unittest.main()
