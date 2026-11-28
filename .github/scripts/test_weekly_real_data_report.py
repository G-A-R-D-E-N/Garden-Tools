import re
import unittest
from datetime import datetime, timezone
from pathlib import Path
from unittest import mock

import weekly_real_data_report as report


class WeeklyReportTests(unittest.TestCase):
    def test_failed_or_cancelled_scheduled_run_is_reported(self):
        now = datetime(2026, 11, 29, 12, 0, tzinfo=timezone.utc)
        for conclusion in ('failure', 'cancelled', 'timed_out', 'startup_failure'):
            run = {'id': 123, 'event': 'schedule', 'status': 'completed',
                   'conclusion': conclusion, 'created_at': '2026-11-29T08:00:00Z'}
            self.assertEqual('failed', report.problem(run, now))

    def test_queued_weekly_run_is_reported_only_after_two_hours(self):
        now = datetime(2026, 11, 29, 12, 0, tzinfo=timezone.utc)
        run = {'id': 123, 'event': 'schedule', 'status': 'queued',
               'created_at': '2026-11-29T10:01:00Z'}
        self.assertIsNone(report.problem(run, now))
        run['created_at'] = '2026-11-29T10:00:00Z'
        self.assertEqual('waiting', report.problem(run, now))

    def test_runs_older_than_the_window_are_not_backfilled(self):
        now = datetime(2026, 12, 20, 12, 0, tzinfo=timezone.utc)
        old = {'event': 'schedule', 'status': 'completed', 'conclusion': 'failure',
               'created_at': '2026-11-29T08:00:00Z', 'updated_at': '2026-11-29T09:00:00Z'}
        self.assertIsNone(report.problem(old, now))
        self.assertEqual('failed', report.problem({**old, 'updated_at': '2026-12-19T09:00:00Z'}, now))

    def test_rerun_waiting_is_timed_from_run_started_at_not_created_at(self):
        now = datetime(2026, 11, 29, 12, 0, tzinfo=timezone.utc)
        run = {'event': 'schedule', 'status': 'queued', 'created_at': '2026-11-27T08:00:00Z',
               'run_started_at': '2026-11-29T11:30:00Z', 'updated_at': '2026-11-29T11:30:00Z'}
        self.assertIsNone(report.problem(run, now))
        run['run_started_at'] = '2026-11-29T09:30:00Z'
        self.assertEqual('waiting', report.problem(run, now))

    def test_success_manual_and_active_run_do_not_report(self):
        now = datetime(2026, 11, 29, 12, 0, tzinfo=timezone.utc)
        for run in (
            {'event': 'schedule', 'status': 'completed', 'conclusion': 'success'},
            {'event': 'workflow_dispatch', 'status': 'completed', 'conclusion': 'failure'},
            {'event': 'schedule', 'status': 'in_progress', 'created_at': '2026-11-29T08:00:00Z'},
        ):
            self.assertIsNone(report.problem(run, now))

    def test_run_and_problem_markers_prevent_duplicate_reports(self):
        self.assertTrue(report.already_reported(['text\n<!-- weekly-real-data:123:1:failed -->'], 123, 'failed'))
        self.assertFalse(report.already_reported(['<!-- weekly-real-data:123:1:waiting -->'], 123, 'failed'))
        self.assertFalse(report.already_reported(['<!-- weekly-real-data:124:1:failed -->'], 123, 'failed'))
        self.assertFalse(report.already_reported(['<!-- weekly-real-data:123:1:failed -->'], 123, 'failed', 2))

    def test_timeout_creates_one_issue_and_polling_does_not_repeat_it(self):
        now = datetime(2026, 11, 29, 12, 0, tzinfo=timezone.utc)
        run = {'id': 123, 'event': 'schedule', 'status': 'completed', 'conclusion': 'timed_out',
               'created_at': '2026-11-29T08:00:00Z', 'html_url': 'https://example.test/runs/123'}
        issues, comments = [], []

        def api(method, path, payload=None):
            if path.endswith('/issues?state=all&per_page=100'):
                return issues
            if path.endswith('/issues/7/comments?per_page=100'):
                return comments
            if method == 'POST' and path.endswith('/issues'):
                issues.append({'number': 7, 'state': 'open', 'title': payload['title'], 'body': payload['body']})
                return issues[-1]
            if method == 'POST' and path.endswith('/issues/7/comments'):
                comments.append(payload)
                return payload
            self.fail(f'unexpected request: {method} {path}')

        report.report_runs([run], now, api)
        report.report_runs([run], now, api)
        self.assertEqual(1, len(issues))
        self.assertEqual([], comments)
        self.assertEqual('The scheduled run ended with conclusion: timed_out.\n\n'
                         'Run: https://example.test/runs/123\n\n'
                         '<!-- weekly-real-data:123:1:failed -->', issues[0]['body'])

    def test_closing_report_does_not_reopen_same_failed_run(self):
        now = datetime(2026, 11, 29, 12, 0, tzinfo=timezone.utc)
        run = {'id': 123, 'event': 'schedule', 'status': 'completed', 'conclusion': 'failure',
               'created_at': '2026-11-29T08:00:00Z', 'html_url': 'https://example.test/runs/123'}
        def api(method, path, payload=None):
            if path.endswith('/issues?state=all&per_page=100'):
                return [{'number': 7, 'state': 'closed', 'title': report.TITLE,
                         'body': report.marker(123, 'failed')}]
            if path.endswith('/issues/7/comments?per_page=100'):
                return []
            self.fail(f'unexpected request: {method} {path}')
        report.report_runs([run], now, api)

    def test_all_scheduled_run_pages_are_checked_after_reporting_outage(self):
        response = '[{"workflow_runs":[{"id":1}]},{"workflow_runs":[{"id":2}]}]'
        with mock.patch.object(report.subprocess, 'run') as command:
            command.return_value.stdout = response
            self.assertEqual([1, 2], [run['id'] for run in report.scheduled_runs()])
            self.assertIn('--paginate', command.call_args.args[0])


class WorkflowReportFormatTests(unittest.TestCase):
    # Detects: the workflow's own report and this monitor must agree on the issue title and marker.
    def test_the_workflow_report_script_is_recognized_by_the_monitor(self):
        script = Path(__file__).resolve().parent / 'report-weekly-real-data-failure.ps1'
        text = script.read_text(encoding='utf-8')
        self.assertEqual(1, text.count('<!-- weekly-real-data:'))
        self.assertEqual(report.TITLE, re.search(r"\$title = '([^']*)'", text).group(1))
        template = re.search(r'"(<!-- weekly-real-data:[^"]*)"', text).group(1)
        body = (template.replace('$($env:GITHUB_RUN_ID)', '123')
                .replace('$($env:GITHUB_RUN_ATTEMPT)', '2'))
        self.assertEqual(report.marker(123, 'failed', 2), body)
        self.assertTrue(report.already_reported([body], 123, 'failed', 2))

    # Detects: the workflow must call the script, and must not keep a second marker of its own.
    def test_the_workflow_step_runs_the_report_script(self):
        workflow = Path(__file__).resolve().parents[2] / '.github' / 'workflows' / 'native-real-data.yml'
        if not workflow.is_file():
            self.skipTest('native-real-data.yml is not exported to the public CI tree')
        text = workflow.read_text(encoding='utf-8')
        self.assertIn('report-weekly-real-data-failure.ps1', text)
        self.assertNotIn('weekly-real-data:', text)


if __name__ == '__main__':
    unittest.main()
