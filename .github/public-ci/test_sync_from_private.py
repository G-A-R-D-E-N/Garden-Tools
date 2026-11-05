import importlib.util
import io
import json
import subprocess
import sys
import tarfile
import unittest
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("sync_from_private", HERE / "sync-from-private.py")
sync = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(sync)
EXPORT_SPEC = importlib.util.spec_from_file_location("export_public_ci", HERE / "export-public-ci.py")
export = importlib.util.module_from_spec(EXPORT_SPEC)
EXPORT_SPEC.loader.exec_module(export)

OLD = "1" * 40
HEAD = "2" * 40
MERGE = "3" * 40
BASE = "4" * 40
DEV = "5" * 40
MAIN = "6" * 40
COMMIT = "7" * 40


def sha(number):
    return f"{number:040x}"


def status(number, context, state, description="", url=""):
    return {"id": number, "context": context, "state": state, "description": description,
            "target_url": url}


def latest(state, description="", url=""):
    return {"state": state, "description": description, "target_url": url}


# Constraint: planning logic stays tested with two branches, though only the integration branch has the public workflows.
SHIPPED_BRANCHES = sync.BRANCHES
sync.BRANCHES = ("nomad/dev", "main")


class ShippedBranchesTest(unittest.TestCase):
    def test_only_the_integration_branch_is_synced(self):
        # Regression: main has no public workflows, so exporting it fails on every tick.
        self.assertEqual(("nomad/dev",), SHIPPED_BRANCHES)


class SourceCommitTest(unittest.TestCase):
    def test_reads_the_trailer_the_export_writes(self):
        self.assertEqual(HEAD, sync.source_commit(sync.export_message("ci/pr-12", HEAD)))

    def test_a_short_or_missing_trailer_is_no_source(self):
        self.assertIsNone(sync.source_commit("Garden Tools ci/pr-12\n\nSource-Commit: " + "2" * 39))
        self.assertIsNone(sync.source_commit("Garden Tools ci/pr-12\n"))

    def test_parses_the_gh_output_lines(self):
        self.assertEqual([(12, HEAD, BASE, "nomad/dev")],
                         sync.parse_pulls(f"12 {HEAD} {BASE} nomad/dev\n"))
        self.assertEqual({"ci/pr-12": HEAD}, sync.parse_refs(f"refs/heads/ci/pr-12 {HEAD}\n"))


class PlanTest(unittest.TestCase):
    def test_branches_come_first_and_pull_requests_follow_by_number(self):
        targets = sync.plan_targets([(13, HEAD, BASE, "nomad/dev"), (12, OLD, BASE, "main")],
                                    {"nomad/dev": DEV, "main": MAIN})
        self.assertEqual(["ci/nomad/dev", "ci/main", "ci/pr-12", "ci/pr-13"],
                         [target.branch for target in targets])
        self.assertEqual("refs/pull/12/merge", targets[2].ref)

    def test_integration_branches_go_before_pull_requests_then_oldest_head_first(self):
        targets = sync.plan_targets([(1, sha(1), BASE, "nomad/dev"), (2, sha(2), BASE, "nomad/dev")],
                                    {"nomad/dev": DEV, "main": MAIN})
        dates = {DEV: "2026-10-03T00:00:00Z", MAIN: "2026-10-04T00:00:00Z",
                 sha(1): "2026-10-02T00:00:00Z", sha(2): "2026-10-01T00:00:00Z"}
        self.assertEqual([DEV, MAIN, sha(2), sha(1)],
                         [target.head for target in sync.oldest_first(targets, dates)])

    def test_stale_pr_branches_name_only_closed_pull_requests(self):
        branches = {"ci/pr-7": OLD, "ci/pr-8": OLD, "ci/nomad/dev": OLD, "ci/pr-x": OLD}
        self.assertEqual(["ci/pr-7"], sync.stale_pr_branches(branches, {8}))

    def test_a_pull_request_merge_is_exported_only_when_its_second_parent_is_the_head(self):
        pull = sync.Target("ci/pr-12", "refs/pull/12/merge", HEAD, 12, BASE, "nomad/dev")
        self.assertTrue(sync.merge_is_current(pull, MERGE, [BASE, HEAD]))
        self.assertFalse(sync.merge_is_current(pull, MERGE, [BASE, OLD]))
        self.assertFalse(sync.merge_is_current(pull, MERGE, [HEAD, BASE]))
        self.assertTrue(sync.merge_is_current(pull, HEAD, []))

    def test_a_branch_is_exported_only_when_its_fetched_head_is_the_listed_head(self):
        branch = sync.Target("ci/main", "refs/heads/main", MAIN, None, None, None)
        self.assertTrue(sync.merge_is_current(branch, MAIN, []))
        self.assertFalse(sync.merge_is_current(branch, MERGE, [MAIN]))


class DueTest(unittest.TestCase):
    def setUp(self):
        self.target = sync.Target("ci/pr-12", "refs/pull/12/merge", HEAD, 12, BASE, "nomad/dev")

    def test_a_head_held_by_a_conflict_is_not_due_whatever_its_branch_says(self):
        conflicted = {"public-ci/export": latest("failure", sync.CONFLICT_TEXT)}
        self.assertFalse(sync.is_due(self.target, OLD, conflicted, {HEAD}))
        self.assertFalse(sync.is_due(self.target, None, conflicted, {HEAD}))

    def test_a_conflict_that_cleared_is_due_again(self):
        conflicted = {"public-ci/export": latest("failure", sync.CONFLICT_TEXT)}
        self.assertTrue(sync.is_due(self.target, None, conflicted, set()))

    def test_a_missing_branch_is_due_until_a_record_exists(self):
        self.assertTrue(sync.is_due(self.target, None, {}, set()))
        pending = {"public-ci/export": latest("pending"), "public-ci/bgs-ci": latest("pending")}
        self.assertTrue(sync.is_due(self.target, None, pending, set()))
        self.assertFalse(sync.is_due(self.target, None, {"public-ci/export": latest("success")}, set()))
        self.assertFalse(sync.is_due(self.target, None, {"public-ci/export": latest("error")}, set()))
        self.assertFalse(sync.is_due(self.target, None, {"public-ci/bgs-ci": latest("failure")}, set()))

    def test_a_branch_is_due_while_it_carries_another_source(self):
        self.assertTrue(sync.is_due(self.target, OLD, {}, set()))
        self.assertFalse(sync.is_due(self.target, HEAD, {}, set()))


class HoldsTest(unittest.TestCase):
    def setUp(self):
        self.target = sync.Target("ci/pr-12", "refs/pull/12/merge", HEAD, 12, BASE, "nomad/dev")
        self.env = sync.base_env("")

    def test_a_conflict_holds_only_while_github_still_reports_it(self):
        conflict = latest("failure", sync.CONFLICT_TEXT)
        with mock.patch.object(sync, "mergeable", return_value="CONFLICTING"):
            self.assertTrue(sync.holds(self.env, self.target, conflict))
        with mock.patch.object(sync, "mergeable", return_value="MERGEABLE"):
            self.assertFalse(sync.holds(self.env, self.target, conflict))

    def test_an_export_failure_holds_only_for_the_same_mergeable_answer(self):
        failed = latest("failure", sync.EXPORT_FAILED_TEXT, "https://run?mergeable=UNKNOWN")
        with mock.patch.object(sync, "mergeable", return_value="UNKNOWN"):
            self.assertTrue(sync.holds(self.env, self.target, failed))
        with mock.patch.object(sync, "mergeable", return_value="CONFLICTING"):
            self.assertFalse(sync.holds(self.env, self.target, failed))

    def test_other_failures_and_successes_never_hold(self):
        with mock.patch.object(sync, "mergeable", return_value="CONFLICTING"):
            self.assertFalse(sync.holds(self.env, self.target, latest("failure", "something else")))
            self.assertFalse(sync.holds(self.env, self.target, latest("success", sync.CONFLICT_TEXT)))


class StatusStatesTest(unittest.TestCase):
    def test_the_newest_status_of_each_context_wins_across_pages(self):
        first_page = json.dumps([status(3, "public-ci/export", "failure", sync.EXPORT_FAILED_TEXT,
                                        "https://run?mergeable=UNKNOWN")])
        second_page = json.dumps([status(2, "public-ci/export", "success"),
                                  status(1, "public-ci/bgs-ci", "pending")])
        with mock.patch.object(sync, "gh", return_value=first_page + second_page):
            states = sync.status_states(sync.base_env(""), HEAD)
        self.assertEqual("failure", states["public-ci/export"]["state"])
        self.assertEqual("https://run?mergeable=UNKNOWN", states["public-ci/export"]["target_url"])
        self.assertEqual("pending", states["public-ci/bgs-ci"]["state"])

    def test_odd_context_names_are_read_as_json_not_lines(self):
        odd = 'public-ci/odd "name" with spaces\nand a newline'
        with mock.patch.object(sync, "gh", return_value=json.dumps([status(1, odd, "success")])):
            states = sync.status_states(sync.base_env(""), HEAD)
        self.assertEqual("success", states[odd]["state"])


class ChecksTest(unittest.TestCase):
    def test_abi_check_runs_only_for_header_or_checker_changes(self):
        self.assertTrue(sync.touches_abi(["Radicle/include/fo4/c_api.h"]))
        self.assertTrue(sync.touches_abi(["Radicle/bindings/csharp/Radicle.Interop/Core.cs"]))
        self.assertTrue(sync.touches_abi([".github/scripts/check_abi_version_collision.py"]))
        self.assertFalse(sync.touches_abi(["Radicle/src/c_api_version.cpp",
                                           "PluginEditTool/native/x.cpp"]))

    def test_only_workflows_that_report_to_the_private_commit_get_a_pending_status(self):
        texts = {"bgs-ci.yml": 'context="public-ci/bgs-ci"',
                 "public-ci-cleanup.yml": "cleanup only"}
        self.assertEqual(["bgs-ci"], sync.status_contexts(texts))

    def test_first_error_drops_paths_and_names(self):
        output = "ok line\nFAIL: test_x (Radicle.tests)\nAssertionError: 'Radicle/src/a.cpp' differs\n"
        self.assertEqual("FAIL: test_x <redacted>", sync.first_error(output))

    def test_no_error_line_gives_an_empty_summary(self):
        self.assertEqual("", sync.first_error("Ran 3 tests\nOK\n"))

    def test_status_post_uses_the_export_context_and_target_url(self):
        env = sync.base_env("")
        with mock.patch.object(sync, "gh") as gh:
            sync.post(env, HEAD, "public-ci/bgs-ci", "pending", "Building on GitHub-hosted runners",
                      "https://github.com/G-A-R-D-E-N/Garden-Tools/actions?query=branch%3Aci/pr-12")
        args = gh.call_args.args
        self.assertIn(f"repos/G-A-R-D-E-N/GardenTools/statuses/{HEAD}", args)
        self.assertIn("context=public-ci/bgs-ci", args)
        self.assertIn("target_url=https://github.com/G-A-R-D-E-N/Garden-Tools/actions"
                      "?query=branch%3Aci/pr-12", args)


class ReportTest(unittest.TestCase):
    def test_an_abi_failure_prints_only_its_exit_code(self):
        with mock.patch.object(sync, "post") as post, redirect_stdout(StringIO()) as out:
            sync.report_abi(sync.base_env(""), "ci/pr-12", HEAD, 2, "https://run")
        self.assertEqual("ci/pr-12 public-ci/abi-version failure: exit 2\n", out.getvalue())
        self.assertEqual(("public-ci/abi-version", "failure"), post.call_args.args[2:4])

    def test_a_private_check_failure_prints_one_redacted_line(self):
        output = "FAIL: test_x (Radicle.tests)\n"
        with mock.patch.object(sync, "post"), redirect_stdout(StringIO()) as out:
            sync.report_private(sync.base_env(""), "ci/pr-12", HEAD, 1, output, "https://run")
        self.assertEqual("ci/pr-12 public-ci/private-only-checks failure: FAIL: test_x <redacted>\n",
                         out.getvalue())


def tar_bytes(members):
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w") as tar:
        for name, data in members:
            info = tarfile.TarInfo(name)
            if data is None:
                info.type = tarfile.SYMTYPE
                info.linkname = "../../../etc/passwd"
                tar.addfile(info)
            else:
                info.size = len(data)
                tar.addfile(info, io.BytesIO(data))
    return buffer.getvalue()


class ScratchTreeTest(unittest.TestCase):
    def test_bundled_tool_inputs_are_regular_files_and_links_are_never_followed(self):
        archive = tar_bytes([("PluginEditTool/tools/bundled-tools.json", b'{"tools": []}'),
                             ("PluginEditTool/tools/link.json", None)])
        with mock.patch.object(sync, "archive", return_value=archive):
            with sync.scratch_tree(sync.base_env(""), MERGE) as tree:
                self.assertEqual(b'{"tools": []}',
                                 (tree / "PluginEditTool/tools/bundled-tools.json").read_bytes())
                self.assertFalse((tree / "PluginEditTool/tools/link.json").exists())
                self.assertFalse((tree / "PluginEditTool/tools/link.json").is_symlink())
                self.assertTrue((tree / sync.BUNDLED_TOOLS).is_file())

    def test_a_tree_path_that_climbs_out_is_refused(self):
        archive = tar_bytes([("PluginEditTool/../../escape", b"x")])
        with mock.patch.object(sync, "archive", return_value=archive):
            with self.assertRaises(sync.Failed):
                with sync.scratch_tree(sync.base_env(""), MERGE):
                    pass


class PublishTest(unittest.TestCase):
    def setUp(self):
        self.target = sync.Target("ci/pr-12", "refs/pull/12/merge", HEAD, 12, BASE, "nomad/dev")
        muted = mock.patch("sys.stdout", new_callable=StringIO)
        muted.start()
        self.addCleanup(muted.stop)
        no_workflows = mock.patch.object(sync, "workflow_texts", return_value={})
        no_workflows.start()
        self.addCleanup(no_workflows.stop)

    def replies(self, **overrides):
        replies = {"list merge ref": f"{MERGE}\trefs/pull/12/merge\n", "fetch": "",
                   "resolve fetched ref": f"{MERGE}\n", "read parents": f"{BASE} {HEAD}\n",
                   "list changed paths": "README.md\n", "export": f"{COMMIT}\n", "push": ""}
        replies.update(overrides)
        return replies

    def fake_run(self, replies):
        calls = []

        def run(step, argv, env):
            calls.append(argv)
            reply = replies[step]
            if isinstance(reply, Exception):
                raise reply
            return reply

        return run, calls

    def test_a_missing_merge_ref_of_a_conflicting_pull_request_is_refused_once(self):
        run, calls = self.fake_run({"list merge ref": ""})
        with mock.patch.object(sync, "run", side_effect=run), \
                mock.patch.object(sync, "mergeable", return_value="CONFLICTING"), \
                mock.patch.object(sync, "post") as post:
            exported = sync.publish(sync.base_env(""), self.target, "", "https://run")
        self.assertFalse(exported)
        self.assertEqual([("public-ci/export", "failure")],
                         [call.args[2:4] for call in post.call_args_list])
        self.assertEqual(1, len(calls))

    def test_a_missing_merge_ref_that_is_not_conflicting_takes_no_status(self):
        run, _ = self.fake_run({"list merge ref": ""})
        for answer in ("UNKNOWN", "MERGEABLE"):
            with self.subTest(mergeable=answer), \
                    mock.patch.object(sync, "run", side_effect=run), \
                    mock.patch.object(sync, "mergeable", return_value=answer), \
                    mock.patch.object(sync, "post") as post:
                exported = sync.publish(sync.base_env(""), self.target, "", "https://run")
            self.assertFalse(exported)
            post.assert_not_called()

    def test_a_stale_merge_ref_that_is_not_conflicting_takes_no_status(self):
        run, calls = self.fake_run(self.replies(**{"read parents": f"{BASE} {OLD}\n"}))
        with mock.patch.object(sync, "run", side_effect=run), \
                mock.patch.object(sync, "mergeable", return_value="MERGEABLE"), \
                mock.patch.object(sync, "post") as post:
            exported = sync.publish(sync.base_env(""), self.target, "", "https://run")
        self.assertFalse(exported)
        post.assert_not_called()
        self.assertFalse(any(argv[:2] == ["git", "push"] for argv in calls))

    def test_a_failed_pending_status_stops_the_push(self):
        run, calls = self.fake_run(self.replies())

        def refuse(env, sha, context, state, description, url):
            raise sync.Failed("post status failed")

        with mock.patch.object(sync, "run", side_effect=run), \
                mock.patch.object(sync, "post", side_effect=refuse):
            with self.assertRaises(sync.Failed):
                sync.publish(sync.base_env(""), self.target, "", "https://run")
        self.assertFalse(any(argv[:2] == ["git", "push"] for argv in calls))

    def test_a_failed_push_is_counted_and_leaves_only_pending_records(self):
        run, _ = self.fake_run(self.replies(push=sync.Failed("push failed")))
        with mock.patch.object(sync, "run", side_effect=run), \
                mock.patch.object(sync, "post") as post:
            with self.assertRaises(sync.PushFailed):
                sync.publish(sync.base_env(""), self.target, "", "https://run")
        self.assertEqual([("public-ci/export", "pending")],
                         [call.args[2:4] for call in post.call_args_list])

    def test_a_failure_after_the_push_posts_error_for_each_unposted_context_and_raises(self):
        run, _ = self.fake_run(self.replies())
        posted = []

        def post(env, sha, context, state, description, url):
            posted.append((context, state))
            if (context, state) == ("public-ci/export", "success"):
                raise sync.Failed("post status failed")

        with mock.patch.object(sync, "run", side_effect=run), mock.patch.object(sync, "post", side_effect=post):
            with self.assertRaises(sync.AfterPush):
                sync.publish(sync.base_env(""), self.target, "", "https://run")
        self.assertEqual([("public-ci/export", "pending"), ("public-ci/export", "success"),
                          ("public-ci/export", "error"), ("public-ci/private-only-checks", "error")],
                         posted)

    def test_an_abi_check_that_was_due_is_errored_too_when_the_push_step_fails_after_it(self):
        run, _ = self.fake_run(self.replies(**{"list changed paths": "Radicle/include/fo4/c_api.h\n",
                                               "deepen history": ""}))
        posted = []

        def post(env, sha, context, state, description, url):
            posted.append((context, state))
            if (context, state) == ("public-ci/export", "success"):
                raise sync.Failed("post status failed")

        with mock.patch.object(sync, "run", side_effect=run), mock.patch.object(sync, "post", side_effect=post):
            with self.assertRaises(sync.AfterPush):
                sync.publish(sync.base_env(""), self.target, "", "https://run")
        self.assertIn(("public-ci/abi-version", "error"), posted)
        self.assertIn(("public-ci/private-only-checks", "error"), posted)


class TokenTest(unittest.TestCase):
    def test_token_stays_in_the_environment_and_a_failure_names_only_the_step(self):
        env = sync.base_env("tok-secret")
        error = subprocess.CalledProcessError(1, ["gh", "api", "tok-secret"], output="tok-secret")
        with mock.patch.object(sync.subprocess, "run", side_effect=error) as run:
            with self.assertRaises(sync.Failed) as raised:
                sync.gh("read branch head", env, "repos/x")
        self.assertEqual("read branch head failed", str(raised.exception))
        self.assertNotIn("tok-secret", " ".join(run.call_args.args[0]))
        self.assertEqual("tok-secret", run.call_args.kwargs["env"]["GH_TOKEN"])

    def test_local_commands_and_checks_never_see_the_token(self):
        env = sync.base_env("tok-secret")
        local = sync.check_env(env)
        self.assertNotIn("tok-secret", local.values())
        self.assertFalse(set(local) & set(sync.TOKEN_KEYS))

    def test_the_abi_child_gets_github_token_but_not_the_gh_token(self):
        target = sync.Target("ci/pr-12", "refs/pull/12/merge", HEAD, 12, BASE, "nomad/dev")
        env = sync.abi_env(sync.base_env("tok-secret"), "tok-secret", target)
        self.assertEqual("tok-secret", env["GITHUB_TOKEN"])
        self.assertNotIn("GH_TOKEN", env)
        self.assertNotIn("TOKEN", env)

    def test_the_runner_token_variable_is_not_passed_on(self):
        with mock.patch.dict(sync.os.environ, {"TOKEN": "tok-secret"}):
            env = sync.base_env("")
        self.assertNotIn("tok-secret", env.values())


class ExportFailedTest(unittest.TestCase):
    def setUp(self):
        self.target = sync.Target("ci/pr-12", "refs/pull/12/merge", HEAD, 12, BASE, "nomad/dev")
        muted = mock.patch("sys.stdout", new_callable=StringIO)
        muted.start()
        self.addCleanup(muted.stop)

    def test_an_export_failure_is_posted_once_per_mergeable_answer(self):
        env = sync.base_env("")
        with mock.patch.object(sync, "mergeable", return_value="UNKNOWN"), \
                mock.patch.object(sync, "status_states", return_value={}), \
                mock.patch.object(sync, "post") as post:
            sync.export_failed(env, self.target, "https://run")
        self.assertEqual(("public-ci/export", "failure", sync.EXPORT_FAILED_TEXT),
                         post.call_args.args[2:5])
        self.assertTrue(post.call_args.args[5].endswith("?mergeable=UNKNOWN"))

        already = {"public-ci/export": latest("failure", sync.EXPORT_FAILED_TEXT,
                                              "https://run?mergeable=UNKNOWN")}
        with mock.patch.object(sync, "mergeable", return_value="UNKNOWN"), \
                mock.patch.object(sync, "status_states", return_value=already), \
                mock.patch.object(sync, "post") as post:
            sync.export_failed(env, self.target, "https://run")
        post.assert_not_called()

        with mock.patch.object(sync, "mergeable", return_value="CONFLICTING"), \
                mock.patch.object(sync, "status_states", return_value=already), \
                mock.patch.object(sync, "post") as post:
            sync.export_failed(env, self.target, "https://run")
        post.assert_called_once()


class TickTest(unittest.TestCase):
    def tick(self, publish):
        env = {"TOKEN": "t", "GITHUB_SERVER_URL": "https://github.com",
               "GITHUB_REPOSITORY": "G-A-R-D-E-N/Garden-Tools", "GITHUB_RUN_ID": "1"}
        patches = [
            mock.patch.dict(sync.os.environ, env),
            mock.patch.object(sync, "open_pulls", return_value=[(12, HEAD, BASE, "nomad/dev")]),
            mock.patch.object(sync, "branch_head", side_effect=lambda env, branch: {
                "nomad/dev": DEV, "main": MAIN}[branch]),
            mock.patch.object(sync, "public_branches", return_value={}),
            mock.patch.object(sync, "status_states", return_value={}),
            mock.patch.object(sync, "head_date", return_value="2026-10-01T00:00:00Z"),
            mock.patch.object(sync, "mergeable", return_value="UNKNOWN"),
            mock.patch.object(sync, "post"),
            mock.patch.object(sync, "publish", side_effect=publish),
        ]
        started = [patch.start() for patch in patches]
        for patch in patches:
            self.addCleanup(patch.stop)
        with redirect_stdout(StringIO()) as out:
            code = sync.sync(dry_run=False)
        return code, out.getvalue(), started[-2], started[-1]

    def test_a_push_failure_is_counted_once_per_tick_and_not_posted_as_a_failure(self):
        attempts = []

        def publish(env, target, token, url):
            attempts.append(target.number)
            if target.number == 12:
                raise sync.PushFailed("push failed")
            return True

        code, out, post, _ = self.tick(publish)
        self.assertEqual([None, None, 12], attempts)
        self.assertEqual(1, code)
        self.assertIn("ci/pr-12 failed: push failed", out)
        post.assert_not_called()

    def test_a_failure_before_the_push_posts_the_export_failure_once(self):
        def publish(env, target, token, url):
            if target.number == 12:
                raise sync.Failed("fetch failed")
            return True

        code, _, post, _ = self.tick(publish)
        self.assertEqual(1, code)
        self.assertEqual(1, post.call_count)
        self.assertEqual(("public-ci/export", "failure", sync.EXPORT_FAILED_TEXT),
                         post.call_args.args[2:5])

    def test_a_failure_after_the_push_is_not_reposted_as_an_export_failure(self):
        def publish(env, target, token, url):
            if target.number == 12:
                raise sync.AfterPush("post-push step failed; see the public sync run")
            return True

        code, _, post, _ = self.tick(publish)
        self.assertEqual(1, code)
        post.assert_not_called()


class ExportGuardTest(unittest.TestCase):
    def test_a_path_with_a_newline_stops_the_export_without_echoing_it(self):
        listing = f"100644 blob {OLD}\ta\nb\0"
        with mock.patch.object(export, "git", return_value=listing), \
                mock.patch.object(sys, "argv", ["export", "HEAD", "message"]):
            with self.assertRaises(SystemExit) as raised:
                export.main()
        self.assertEqual("export refuses a path with a newline", raised.exception.code)


class WorkflowTest(unittest.TestCase):
    def test_the_public_job_uses_no_checkout_action_and_fetches_one_branch_quietly(self):
        text = (HERE / "public-main" / "sync-from-private.yml").read_text(encoding="utf-8")
        self.assertNotIn("actions/checkout", text)
        self.assertIn("git fetch --quiet --no-tags --depth=1 origin refs/heads/nomad/dev", text)


class DryRunTest(unittest.TestCase):
    def dry_run_lines(self, replies):
        def fake_run(argv, **kwargs):
            command = " ".join(argv)
            for needle, stdout in replies.items():
                if needle in command:
                    return subprocess.CompletedProcess(argv, 0, stdout, "")
            raise AssertionError("dry run issued an unexpected command")

        with mock.patch.object(sync.subprocess, "run", side_effect=fake_run):
            with redirect_stdout(StringIO()) as out:
                code = sync.sync(dry_run=True)
        self.assertEqual(0, code)
        return out.getvalue().splitlines()

    def test_dry_run_plans_each_target_from_listed_state_and_writes_nothing(self):
        replies = {
            "git/ref/heads/nomad/dev": f"{DEV}\n",
            "git/ref/heads/main": f"{MAIN}\n",
            "pulls?state=open": f"12 {HEAD} {BASE} nomad/dev\n",
            "matching-refs": f"refs/heads/ci/pr-12 {OLD}\nrefs/heads/ci/pr-99 {OLD}\n",
            f"git/commits/{OLD}": f"Garden Tools ci/pr-12\n\nSource-Commit: {HEAD}\n",
            f"git/commits/{DEV}": "2026-10-31T00:00:00Z\n",
            f"git/commits/{MAIN}": "2026-10-31T00:00:00Z\n",
            f"repos/{sync.PRIVATE}/commits/": "[]",
        }
        self.assertEqual(["ci/nomad/dev export", "ci/main export", "ci/pr-12 current",
                          "ci/pr-99 delete"], self.dry_run_lines(replies))

    def test_dry_run_exports_the_five_oldest_due_heads_and_marks_held_ones(self):
        pulls = "".join(f"{n} {sha(n)} {BASE} nomad/dev\n" for n in range(1, 9))
        conflict = json.dumps([status(2, "public-ci/export", "failure", sync.CONFLICT_TEXT)])
        replies = {
            "git/ref/heads/nomad/dev": f"{DEV}\n",
            "git/ref/heads/main": f"{MAIN}\n",
            "pulls?state=open": pulls,
            "matching-refs": "",
            f"repos/{sync.PRIVATE}/commits/{sha(8)}/statuses": conflict,
            "graphql": "CONFLICTING\n",
            f"repos/{sync.PRIVATE}/commits/": "[]",
            f"git/commits/{DEV}": "2026-10-31T00:00:00Z\n",
            f"git/commits/{MAIN}": "2026-10-31T00:00:00Z\n",
        }
        for n in range(1, 8):
            replies[f"git/commits/{sha(n)}"] = f"2026-10-{n:02d}T00:00:00Z\n"
        self.assertEqual([
            "ci/nomad/dev queued", "ci/main queued",
            "ci/pr-1 export", "ci/pr-2 export", "ci/pr-3 export", "ci/pr-4 export",
            "ci/pr-5 export", "ci/pr-6 queued", "ci/pr-7 queued", "ci/pr-8 handled",
        ], self.dry_run_lines(replies))

    def test_a_held_head_becomes_due_again_once_github_reports_it_mergeable(self):
        conflict = json.dumps([status(2, "public-ci/export", "failure", sync.CONFLICT_TEXT)])
        replies = {
            "git/ref/heads/nomad/dev": f"{DEV}\n",
            "git/ref/heads/main": f"{MAIN}\n",
            "pulls?state=open": f"8 {sha(8)} {BASE} nomad/dev\n",
            "matching-refs": "",
            f"repos/{sync.PRIVATE}/commits/{sha(8)}/statuses": conflict,
            "graphql": "MERGEABLE\n",
            f"repos/{sync.PRIVATE}/commits/": "[]",
            f"git/commits/{DEV}": "2026-10-31T00:00:00Z\n",
            f"git/commits/{MAIN}": "2026-10-31T00:00:00Z\n",
            f"git/commits/{sha(8)}": "2026-10-08T00:00:00Z\n",
        }
        self.assertEqual(["ci/nomad/dev export", "ci/main export", "ci/pr-8 export"],
                         self.dry_run_lines(replies))


if __name__ == "__main__":
    unittest.main()
