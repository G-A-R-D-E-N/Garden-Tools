import unittest

import ci_critical_path


def job(name, started, finished):
    return {"name": name, "started_at": started, "completed_at": finished}


class SpineTest(unittest.TestCase):
    def test_parallel_jobs_are_measured_from_first_start_to_last_finish_not_summed(self):
        jobs = [
            job("Build", "2026-11-16T10:00:00Z", "2026-11-16T10:30:00Z"),
            job("Test", "2026-11-16T10:20:00Z", "2026-11-16T10:40:00Z"),
        ]
        shape = ci_critical_path.spine(jobs)
        self.assertEqual(40.0, shape["wall_minutes"])
        self.assertEqual(50.0, shape["busiest_minutes"])
        self.assertEqual(10.0, shape["parallel_gain_minutes"])
        self.assertEqual("Build", shape["slowest_job"])

    def test_a_job_that_never_finished_does_not_shrink_the_measured_wait(self):
        jobs = [
            job("Build", "2026-11-16T10:00:00Z", "2026-11-16T10:30:00Z"),
            {"name": "Test", "started_at": "2026-11-16T10:31:00Z", "completed_at": None},
        ]
        shape = ci_critical_path.spine(jobs)
        self.assertEqual(30.0, shape["wall_minutes"])
        self.assertEqual("Build", shape["slowest_job"])

    def test_a_run_with_no_job_timings_measures_nothing_rather_than_zero(self):
        self.assertIsNone(ci_critical_path.spine([{"name": "Build", "status": "queued"}]))

    def test_a_missing_timestamp_is_not_measured_at_all(self):
        self.assertEqual(2.0, ci_critical_path.minutes("2026-11-16T10:00:00Z", "2026-11-16T10:02:00Z"))
        self.assertIsNone(ci_critical_path.minutes(None, "2026-11-16T10:02:00Z"))


if __name__ == "__main__":
    unittest.main()
