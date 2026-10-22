#!/usr/bin/env python3
"""Validation of the Run On rows in condition_function_codegen.py."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import condition_function_codegen  # noqa: E402

SCHEMA = Path(__file__).resolve().parent.parent / "schema" / "fallout4" / "condition_functions.json"


def run_on(**overrides):
    row = {
        "value": 0,
        "name": "Subject",
        "parameter3": "integer",
        "reference_form_id": False,
    }
    row.update(overrides)
    return row


class RunOnRowTests(unittest.TestCase):
    def load(self, *rows):
        return condition_function_codegen.load_run_on(list(rows), {"integer", "alias", "event_data_code"})

    def rejects(self, message, *rows):
        with self.assertRaisesRegex(ValueError, message):
            self.load(*rows)

    def test_committed_schema_run_on_rows_are_valid(self):
        types, functions, rows = condition_function_codegen.load_table(SCHEMA)
        self.assertEqual(len(types), 51)
        self.assertEqual(len(functions), 479)
        self.assertEqual([row["value"] for row in rows], list(range(11)))
        self.assertEqual(rows[2]["reference_form_id"], True)
        self.assertEqual(rows[5]["parameter3"], "alias")
        self.assertEqual(rows[7]["parameter3"], "event_data_code")

    def test_values_must_be_unique_and_increasing(self):
        self.assertEqual(len(self.load(run_on(value=0), run_on(value=1, name="Target"))), 2)
        self.rejects("duplicate Run On value", run_on(value=0), run_on(value=0, name="Target"))
        self.rejects("ascending", run_on(value=1, name="Target"), run_on(value=0))

    def test_parameter3_must_name_a_parameter_type(self):
        self.rejects("unknown parameter type", run_on(parameter3="event"))

    def test_reference_form_id_must_be_a_boolean(self):
        for value in ("yes", 1, 0, None):
            with self.subTest(value=value):
                self.rejects("reference_form_id must be a boolean", run_on(reference_form_id=value))

    def test_rows_reject_unknown_keys_and_empty_names(self):
        self.rejects("run_on entries must be objects with", run_on(targets=[]))
        self.rejects("name is invalid", run_on(name=""))


if __name__ == "__main__":
    unittest.main()
