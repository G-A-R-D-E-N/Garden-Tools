#!/usr/bin/env python3
"""Validation tests for the storage and explicit anchor keys of formlink_lists rows in formlink_schema_codegen.py."""

import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import formlink_schema_codegen  # noqa: E402


def load(row):
    return load_rows([row])


def load_rows(rows):
    document = {
        "version": 1,
        "records": [{"signature": "TEST", "fields": [], "formlink_lists": rows}],
        "keyword_records": [],
        "target_unions": [{"name": "sigBaseObjects", "targets": ["STAT"]}],
    }
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "fields.json"
        path.write_text(json.dumps(document), encoding="utf-8")
        return formlink_schema_codegen.load_lists(path)[0]


def row(**overrides):
    base = {
        "path": "TEST\\Links",
        "label": "Links",
        "subrecord": "PNAM",
        "targets": ["STAT"],
        "nullable": False,
        "mutation": "appendable",
    }
    base.update(overrides)
    return base


def segment(**overrides):
    base = {
        "storage": "packed_array",
        "counter": "CNTS",
        "counter_payload_size": 12,
        "segment_counters": [8, 4, 0],
        "segment_index": 0,
    }
    base.update(overrides)
    return row(**base)


class FormLinkListStorageTests(unittest.TestCase):
    def test_storage_defaults_to_per_subrecord(self):
        self.assertEqual(load(row())[0]["storage"], "per_subrecord")

    def test_packed_array_storage_is_accepted(self):
        self.assertEqual(load(row(storage="packed_array"))[0]["storage"], "packed_array")

    def test_unknown_storage_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "unsupported FormLink list storage"):
            load(row(storage="bogus"))

    def test_explicit_anchor_predecessors_replace_sibling_order(self):
        anchored = load(row(storage="packed_array", anchor_predecessors=["EDID"]))[0]
        self.assertEqual(anchored["anchor_predecessors"], ["EDID"])
        self.assertEqual(anchored["anchor_successors"], [])

    def test_explicit_anchor_must_be_signatures(self):
        with self.assertRaisesRegex(ValueError, "anchor_predecessors must be signatures"):
            load(row(anchor_predecessors=["EDIDX"]))


class FormLinkListSegmentTests(unittest.TestCase):
    def test_segment_keys_are_parsed_together(self):
        parsed = load(segment())[0]
        self.assertEqual(parsed["counter"], "CNTS")
        self.assertEqual(parsed["segment"]["payload_size"], 12)
        self.assertEqual(parsed["segment"]["counters"], [8, 4, 0])
        self.assertEqual(parsed["segment"]["index"], 0)

    def test_segment_keys_must_be_complete(self):
        with self.assertRaisesRegex(ValueError, "segment keys must be declared together"):
            load(row(storage="packed_array", counter="CNTS", segment_index=0))

    def test_segment_requires_packed_array_storage(self):
        with self.assertRaisesRegex(ValueError, "segments require packed_array storage"):
            load(segment(storage="per_subrecord"))

    def test_segment_requires_a_counter(self):
        with self.assertRaisesRegex(ValueError, "segments require a counter subrecord"):
            load(segment(counter=""))

    def test_counter_payload_size_must_be_a_multiple_of_four(self):
        with self.assertRaisesRegex(ValueError, "counter_payload_size must be a positive multiple of four"):
            load(segment(counter_payload_size=10, segment_counters=[0, 4, 8]))

    def test_segment_counters_must_fit_the_payload(self):
        with self.assertRaisesRegex(ValueError, "must fit the counter payload"):
            load(segment(counter_payload_size=8, segment_counters=[8, 4, 0], segment_index=0))

    def test_segment_counters_must_not_repeat(self):
        with self.assertRaisesRegex(ValueError, "duplicate offsets"):
            load(segment(segment_counters=[4, 4, 0]))

    def test_segment_index_must_index_the_counters(self):
        with self.assertRaisesRegex(ValueError, "segment_index must index segment_counters"):
            load(segment(segment_index=3))

    def test_sibling_segments_must_share_one_counter_layout(self):
        with self.assertRaisesRegex(ValueError, "must share one counter layout"):
            load_rows([segment(segment_index=0), segment(path="TEST\\More", segment_index=1, segment_counters=[8, 0])])

    def test_sibling_segments_may_share_one_subrecord(self):
        rows = load_rows([segment(segment_index=0), segment(path="TEST\\More", segment_index=1)])
        self.assertEqual([entry["segment"]["index"] for entry in rows], [0, 1])

    def test_unsegmented_rows_still_may_not_share_a_subrecord(self):
        with self.assertRaisesRegex(ValueError, "duplicate FormLink list subrecord mapping"):
            load_rows([row(), row(path="TEST\\More")])

    def test_segmented_and_unsegmented_rows_may_not_share_a_subrecord(self):
        with self.assertRaisesRegex(ValueError, "duplicate FormLink list subrecord mapping"):
            load_rows([segment(segment_index=0), row(path="TEST\\More")])

    def test_render_emits_the_segment_table_and_descriptor_fields(self):
        rendered, shards = formlink_schema_codegen.render(
            load_rows([segment(segment_index=1)]), [], [], [])
        self.assertEqual(shards, {})
        self.assertIn("k_generated_packed_segment_counters[] = {\n  8,\n  4,\n  0,\n};", rendered)
        self.assertIn("Signature::from_string(\"CNTS\"), false, true, false, true, 0, 1, 0, 0, 0, 1, 0, 3, 12, 0, Signature{}}",
                      rendered)

    def test_counter_offset_requires_a_counter_and_no_segments(self):
        with self.assertRaisesRegex(ValueError, "requires a counter subrecord"):
            load_rows([row(path="TEST\\Counter", counter_offset=20)])
        with self.assertRaisesRegex(ValueError, "cannot be combined with segments"):
            load_rows([segment(segment_index=0, counter_offset=20)])
        rows = load_rows([row(path="TEST\\Counter", counter="CNTS", counter_offset=20)])
        self.assertEqual(rows[0]["counter_offset"], 20)

    def test_render_emits_the_counter_offset(self):
        rendered, shards = formlink_schema_codegen.render(
            load_rows([row(path="TEST\\Counter", counter="CNTS", counter_offset=20)]), [], [], [])
        self.assertEqual(shards, {})
        self.assertIn("Signature::from_string(\"CNTS\"), false, true, false, true, 0, 1, 0, 0, 0, 0, 0, 0, 0, 20, Signature{}}",
                      rendered)

    def test_element_tail_is_emitted_and_requires_per_subrecord_storage(self):
        rendered, shards = formlink_schema_codegen.render(
            load_rows([row(element_tail="RNAM")]), [], [], [])
        self.assertEqual(shards, {})
        self.assertIn('0, 0, 0, 0, 0, 0, 0, Signature::from_string("RNAM")}', rendered)
        with self.assertRaisesRegex(ValueError, "element_tail requires per_subrecord storage"):
            load(segment(element_tail="RNAM"))

    def test_render_keeps_a_placeholder_without_segments(self):
        rendered, shards = formlink_schema_codegen.render(load_rows([row()]), [], [], [])
        self.assertEqual(shards, {})
        self.assertIn("k_generated_packed_segment_counters[] = {\n  std::uint32_t{},\n};", rendered)

    def test_render_shards_whole_records_below_the_line_limit(self):
        document = {
            "version": 1,
            "records": [
                {"signature": record, "fields": [], "formlink_lists": [
                    row(path=f"{record}\\Links", subrecord="PNAM",
                        targets=[f"T{index:03d}" for index in range(400)])]}
                for record in ("TEST", "TESU")
            ],
            "keyword_records": [],
            "target_unions": [{"name": "sigBaseObjects", "targets": ["STAT"]}],
        }
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fields.json"
            path.write_text(json.dumps(document), encoding="utf-8")
            lists, nested, fields, unions = formlink_schema_codegen.load_lists(path)
        rendered, shards = formlink_schema_codegen.render(lists, nested, fields, unions)
        names = sorted(shards)
        self.assertEqual(names, ["generated_formlink_lists_formlink_list_targets_shard00.inc",
                                 "generated_formlink_lists_formlink_list_targets_shard01.inc"])
        self.assertEqual([line for line in rendered.splitlines() if line.startswith("#include")],
                         [f'#include "{name}"' for name in names])
        for record, name in zip(("TEST", "TESU"), names):
            self.assertIn(f": records {record} to {record}, 400 entries.", shards[name])
            self.assertEqual(sum(1 for line in shards[name].splitlines()
                                 if line.startswith("  Signature::from_string")), 400)
        formlink_schema_codegen.render_shard("k_generated_test", 0, 1,
                                             [("TEST", ["  Signature{},"] * 747)])
        with self.assertRaisesRegex(ValueError, "exceeds 750 lines"):
            formlink_schema_codegen.render_shard("k_generated_test", 0, 1,
                                                 [("TEST", ["  Signature{},"] * 748)])


class FormLinkShardCheckTests(unittest.TestCase):
    """--check must fail on a shard that drifted or that the schema no longer produces."""

    def run_tool(self, *arguments):
        with mock.patch("sys.argv", ["formlink_schema_codegen.py", *arguments]), \
                contextlib.redirect_stdout(io.StringIO()) as captured:
            status = formlink_schema_codegen.main()
        return status, captured.getvalue()

    def written(self, directory):
        document = {
            "version": 1,
            "records": [
                {"signature": record, "fields": [], "formlink_lists": [
                    row(path=f"{record}\\Links", subrecord="PNAM",
                        targets=[f"T{index:03d}" for index in range(400)])]}
                for record in ("TEST", "TESU")
            ],
            "keyword_records": [],
            "target_unions": [{"name": "sigBaseObjects", "targets": ["STAT"]}],
        }
        schema = Path(directory) / "fields.json"
        schema.write_text(json.dumps(document), encoding="utf-8")
        output = Path(directory) / "generated_formlink_lists.inc"
        status, text = self.run_tool("--schema", str(schema), "--output", str(output))
        self.assertEqual((status, text), (0, ""))
        return schema, output

    def test_check_reports_a_stale_shard_and_the_writer_repairs_it(self):
        with tempfile.TemporaryDirectory() as directory:
            schema, output = self.written(directory)
            shards = formlink_schema_codegen.shard_files(output)
            self.assertEqual(len(shards), 2, shards)
            drifted = shards[0]
            drifted.write_text(drifted.read_text(encoding="utf-8") + "  // drift\n", encoding="utf-8")

            status, text = self.run_tool("--schema", str(schema), "--output", str(output), "--check")
            self.assertEqual(status, 1)
            self.assertIn(f"stale: {drifted}", text)
            self.assertNotIn(f"stale: {output}", text)

            self.assertEqual(self.run_tool("--schema", str(schema), "--output", str(output))[0], 0)
            self.assertEqual(self.run_tool("--schema", str(schema), "--output", str(output),
                                           "--check"), (0, ""))

    def test_check_reports_an_orphan_shard_and_the_writer_removes_it(self):
        with tempfile.TemporaryDirectory() as directory:
            schema, output = self.written(directory)
            orphan = output.with_name("generated_formlink_lists_formlink_list_targets_shard02.inc")
            orphan.write_text('  Signature::from_string("STAT"),\n', encoding="utf-8")

            status, text = self.run_tool("--schema", str(schema), "--output", str(output), "--check")
            self.assertEqual(status, 1)
            self.assertIn(f"orphan shard: {orphan}", text)

            self.assertEqual(self.run_tool("--schema", str(schema), "--output", str(output))[0], 0)
            self.assertFalse(orphan.exists())
            self.assertEqual(self.run_tool("--schema", str(schema), "--output", str(output),
                                           "--check"), (0, ""))


if __name__ == "__main__":
    unittest.main()
