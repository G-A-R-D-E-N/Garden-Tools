#!/usr/bin/env python3
"""Validation tests for the storage and explicit anchor keys of formlink_lists rows in formlink_schema_codegen.py."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

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
        rendered = formlink_schema_codegen.render(load_rows([segment(segment_index=1)]), [], [], [])
        self.assertIn("k_generated_packed_segment_counters[] = {\n  8,\n  4,\n  0,\n};", rendered)
        self.assertIn("Signature::from_string(\"CNTS\"), false, true, false, true, 0, 1, 0, 0, 0, 1, 0, 3, 12, Signature{}}",
                      rendered)

    def test_element_tail_is_emitted_and_requires_per_subrecord_storage(self):
        rendered = formlink_schema_codegen.render(load_rows([row(element_tail="RNAM")]), [], [], [])
        self.assertIn('0, 0, 0, 0, 0, 0, 0, Signature::from_string("RNAM")}', rendered)
        with self.assertRaisesRegex(ValueError, "element_tail requires per_subrecord storage"):
            load(segment(element_tail="RNAM"))

    def test_render_keeps_a_placeholder_without_segments(self):
        rendered = formlink_schema_codegen.render(load_rows([row()]), [], [], [])
        self.assertIn("k_generated_packed_segment_counters[] = {\n  std::uint32_t{},\n};", rendered)


if __name__ == "__main__":
    unittest.main()
