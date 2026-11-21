import os
import tempfile
import unittest
from contextlib import redirect_stdout
from io import StringIO
from unittest import mock

import ci_timings


def job(name, started, finished, conclusion="success", status="completed"):
    return {"name": name, "conclusion": conclusion, "status": status, "started_at": started, "completed_at": finished}


class MinutesTest(unittest.TestCase):
    def test_duration_is_finished_minus_started_in_minutes(self):
        self.assertEqual(12.5, ci_timings.minutes(job("a", "2026-11-16T10:00:00Z", "2026-11-16T10:12:30Z")))

    def test_a_job_missing_either_timestamp_has_no_duration(self):
        self.assertIsNone(ci_timings.minutes(job("a", None, "2026-11-16T10:12:30Z")))
        self.assertIsNone(ci_timings.minutes(job("a", "2026-11-16T10:00:00Z", None)))


class TableTest(unittest.TestCase):
    def test_rows_show_result_and_one_decimal_minutes(self):
        rows = [
            job("Build", "2026-11-16T10:00:00Z", "2026-11-16T10:12:30Z"),
            {"name": "Test", "conclusion": None, "status": "in_progress", "started_at": "2026-11-16T10:00:00Z", "completed_at": None},
            {"started_at": None},
        ]
        self.assertEqual(
            "| Job | Result | Minutes |\n|---|---|---|\n| Build | success | 12.5 |\n| Test | in_progress | - |\n| ? | ? | - |",
            ci_timings.table(rows),
        )


class ReportTest(unittest.TestCase):
    ROWS = [job("Build", "2026-11-16T10:00:00Z", "2026-11-16T10:12:30Z"), job("Skipped", None, None, conclusion="skipped")]

    def test_prints_only_finished_jobs_and_appends_the_table_to_the_step_summary(self):
        with tempfile.TemporaryDirectory() as tmp:
            summary = os.path.join(tmp, "summary.md")
            with open(summary, "w", encoding="utf-8") as handle:
                handle.write("earlier\n")
            out = StringIO()
            with mock.patch.dict(os.environ, {"GITHUB_STEP_SUMMARY": summary}), redirect_stdout(out):
                text = ci_timings.report(self.ROWS)
            self.assertEqual("  12.5 min  Build\n", out.getvalue())
            with open(summary, encoding="utf-8") as handle:
                self.assertEqual("earlier\n" + text + "\n", handle.read())

    def test_without_a_step_summary_the_table_is_still_returned(self):
        env = {k: v for k, v in os.environ.items() if k != "GITHUB_STEP_SUMMARY"}
        with mock.patch.dict(os.environ, env, clear=True), redirect_stdout(StringIO()):
            self.assertIn("| Build | success | 12.5 |", ci_timings.report(self.ROWS))


class SlowStepsTest(unittest.TestCase):
    STEPS = [
        {"name": "Checkout", "started_at": "2026-11-16T10:00:00Z", "completed_at": "2026-11-16T10:00:20Z"},
        {"name": "Build", "started_at": "2026-11-16T10:01:00Z", "completed_at": "2026-11-16T10:13:30Z"},
        {"name": "Test", "started_at": "2026-11-16T10:14:00Z", "completed_at": "2026-11-16T10:15:00Z"},
        {"name": "Skipped", "started_at": None, "completed_at": None},
    ]

    def rows(self):
        return [{**job("Native", "2026-11-16T10:00:00Z", "2026-11-16T10:15:00Z"), "steps": self.STEPS}, job("Plain", "2026-11-16T10:00:00Z", "2026-11-16T10:01:00Z")]

    def test_lists_steps_of_a_minute_or_more_slowest_first_and_ignores_jobs_without_steps(self):
        self.assertEqual([("Native", "Build", 12.5), ("Native", "Test", 1.0)], ci_timings.slow_steps(self.rows()))

    def test_report_prints_and_summarises_the_slow_steps_after_the_job_table(self):
        out = StringIO()
        with mock.patch.dict(os.environ, {"GITHUB_STEP_SUMMARY": ""}), redirect_stdout(out):
            text = ci_timings.report(self.rows())
        self.assertIn("| Native | Build | 12.5 |", text)
        self.assertNotIn("Checkout", text)
        self.assertTrue(out.getvalue().endswith("  12.5 min  Native / Build\n   1.0 min  Native / Test\n"))


class JobsTest(unittest.TestCase):
    def run_gh(self, returncode, stdout="", stderr=""):
        done = mock.Mock(returncode=returncode, stdout=stdout, stderr=stderr)
        with mock.patch.object(ci_timings.subprocess, "run", return_value=done) as run:
            return run, ci_timings.jobs("o/r", "42")

    def test_parses_one_json_object_per_line_and_skips_blank_lines(self):
        run, rows = self.run_gh(0, '{"name": "a"}\n\n{"name": "b"}\n')
        self.assertEqual([{"name": "a"}, {"name": "b"}], rows)
        self.assertEqual("repos/o/r/actions/runs/42/jobs", run.call_args.args[0][2])

    def test_gh_failure_raises_with_trimmed_stderr(self):
        with self.assertRaisesRegex(RuntimeError, "^boom$"):
            self.run_gh(1, "", " boom \n")
        with self.assertRaisesRegex(RuntimeError, "gh api failed"):
            self.run_gh(1, "", "")


class MainTest(unittest.TestCase):
    def test_no_run_context_is_a_noop_success(self):
        with mock.patch.dict(os.environ, {"GITHUB_REPOSITORY": "", "GITHUB_RUN_ID": ""}), redirect_stdout(StringIO()) as out:
            self.assertEqual(0, ci_timings.main())
        self.assertEqual("ci-timings: no run context\n", out.getvalue())

    def test_a_failed_job_lookup_is_a_notice_not_a_ci_failure(self):
        with mock.patch.dict(os.environ, {"GITHUB_REPOSITORY": "o/r", "GITHUB_RUN_ID": "42"}), \
                mock.patch.object(ci_timings, "jobs", side_effect=RuntimeError("rate limited")), \
                redirect_stdout(StringIO()) as out:
            self.assertEqual(0, ci_timings.main())
        self.assertEqual("::notice::ci-timings: job list unavailable (rate limited)\n", out.getvalue())


if __name__ == "__main__":
    unittest.main()
