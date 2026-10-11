import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest


EXE = pathlib.Path(sys.argv.pop(1)).resolve()


def text(value):
    data = value.encode("utf-8")
    return struct.pack("<H", len(data)) + data


def subrecord(name, data):
    return name.encode("ascii") + struct.pack("<H", len(data)) + data


def record(name, form, payload, flags=0):
    return struct.pack("<4sIIIHHHH", name.encode("ascii"), len(payload), flags, form, 0, 0, 131, 0) + payload


def vmad(count=1):
    return struct.pack("<HHH", 5, 2, count) + b"".join(text(f"Script{i}") + struct.pack("<BH", 0, 0) for i in range(count))


def plugin(records, master=None):
    header = subrecord("HEDR", struct.pack("<fII", 1.0, len(records), 0x900))
    if master:
        header += subrecord("MAST", master.encode("utf-8") + b"\0") + subrecord("DATA", bytes(8))
    groups = []
    for signature, form, data, flags in records:
        body = record(signature, form, subrecord("VMAD", data), flags)
        groups.append(struct.pack("<4sI4sIHHHH", b"GRUP", 24 + len(body), signature.encode("ascii"), 0, 0, 0, 0, 0) + body)
    return record("TES4", 0, header) + b"".join(groups)


class PapyrusDumpTests(unittest.TestCase):
    def run_dump(self, first, second=None, block_output=False):
        temporary = tempfile.TemporaryDirectory(prefix="radicle-papyrus-")
        self.addCleanup(temporary.cleanup)
        root = pathlib.Path(temporary.name)
        paths = [root / "First.esp"]
        paths[0].write_bytes(plugin(first))
        if second is not None:
            paths.append(root / "Second.esp")
            paths[1].write_bytes(plugin(second, "First.esp"))
        order = root / "order.tsv"
        order.write_text("".join(f"{i}\t{path}\n" for i, path in enumerate(paths)), encoding="utf-8")
        control = subprocess.run([str(EXE), "versions", str(order), str(root / "control")], capture_output=True, text=True)
        self.assertEqual(0, control.returncode, control.stderr)
        output = root / "measurement"
        if block_output:
            (output / block_output).mkdir(parents=True)
        result = subprocess.run([str(EXE), "papyrus", str(order), str(output)], capture_output=True, text=True)
        return result, output

    def test_effective_winners_and_deleted_owners(self):
        result, output = self.run_dump(
            [("ACTI", 0x800, vmad(2), 0), ("NPC_", 0x801, vmad(), 0)],
            [("ACTI", 0x800, vmad(), 0), ("NPC_", 0x801, vmad(), 0x20), ("REFR", 0x01000800, vmad(2), 0)],
        )
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual("000800:first.esp\tACTI\t1\t0\t0\t0\n000800:second.esp\tREFR\t2\t0\t0\t0\n",
                         (output / "papyrus.tsv").read_text(encoding="utf-8"))
        self.assertEqual("", (output / "papyrus_failures.tsv").read_text(encoding="utf-8"))

    def test_quest_fragments_and_alias_scripts(self):
        extras = struct.pack("<BH", 0, 1) + text("QF_Probe") + struct.pack("<BH", 0, 0)
        extras += struct.pack("<HHIB", 10, 0, 0, 0) + text("QF_Probe") + text("Fragment_10")
        extras += struct.pack("<HHHIHHH", 1, 0, 5, 0, 0, 2, 1) + text("AliasScript") + struct.pack("<BH", 0, 0)
        result, output = self.run_dump([("QUST", 0x800, vmad() + extras, 0)])
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual("000800:first.esp\tQUST\t1\t1\t1\t1\n", (output / "papyrus.tsv").read_text(encoding="utf-8"))
        self.assertTrue((output / "papyrus_fragments.tsv").is_file())
        self.assertEqual("000800:first.esp\tQUST\t1\t1\n", (output / "papyrus_fragments.tsv").read_text(encoding="utf-8"))

    def test_indexed_fragment_counts_and_empty_header_script(self):
        for signature in ("PERK", "TERM"):
            for header in ("Fragments", ""):
                with self.subTest(signature=signature, header=header):
                    extras = bytes([3]) + text(header) + struct.pack("<BH", 0, 0)
                    extras += struct.pack("<H", 2)
                    extras += b"".join(struct.pack("<HHB", index, 0, 0) + text("Fragments") + text(f"Fragment_{index}")
                                       for index in range(2))
                    result, output = self.run_dump([(signature, 0x800, vmad() + extras, 0)])
                    self.assertEqual(0, result.returncode, result.stderr)
                    self.assertTrue((output / "papyrus_fragments.tsv").is_file())
                    self.assertEqual(f"000800:first.esp\t{signature}\t2\t{int(bool(header))}\n",
                                     (output / "papyrus_fragments.tsv").read_text(encoding="utf-8"))
                    self.assertEqual(f"000800:first.esp\t{signature}\t1\t0\t0\t0\n",
                                     (output / "papyrus.tsv").read_text(encoding="utf-8"))

    def test_flagged_fragments_and_scene_phase_entries(self):
        for signature in ("PACK", "INFO", "SCEN"):
            with self.subTest(signature=signature):
                flags = 0b101 if signature == "PACK" else 0b11
                extras = bytes([3, flags]) + text("Fragments") + struct.pack("<BH", 0, 0)
                extras += (bytes([0]) + text("Fragments") + text("Fragment")) * 2
                if signature == "SCEN":
                    extras += struct.pack("<H", 3)
                    extras += (struct.pack("<BBHBB", 0, 0, 0, 0, 0) + text("Fragments") + text("Phase")) * 3
                result, output = self.run_dump([(signature, 0x800, vmad() + extras, 0), ("ACTI", 0x801, vmad(), 0)])
                self.assertEqual(0, result.returncode, result.stderr)
                self.assertTrue((output / "papyrus_fragments.tsv").is_file())
                self.assertEqual(f"000800:first.esp\t{signature}\t{5 if signature == 'SCEN' else 2}\t1\n"
                                 "000801:first.esp\tACTI\t0\t0\n",
                                 (output / "papyrus_fragments.tsv").read_text(encoding="utf-8"))

    def test_partial_fragment_walk_never_exports_counts(self):
        for signature in ("PERK", "TERM", "PACK", "INFO", "SCEN"):
            with self.subTest(signature=signature):
                header = bytes([3]) + (bytes([1]) if signature in ("PACK", "INFO", "SCEN") else b"")
                extras = header + text("Fragments") + struct.pack("<BH", 0, 0)
                extras += struct.pack("<HHHB", 1, 0, 0, 0) if signature in ("PERK", "TERM") else bytes([0])
                extras += text("Fragments") + text("Fragment")[:-1]
                result, output = self.run_dump([(signature, 0x800, vmad() + extras, 0)])
                self.assertEqual(1, result.returncode, result.stderr)
                self.assertTrue((output / "papyrus_fragments.tsv").is_file())
                self.assertEqual("", (output / "papyrus_fragments.tsv").read_text(encoding="utf-8"))

    def test_incomplete_vmad_is_not_a_complete_measurement(self):
        variable = struct.pack("<HHH", 5, 2, 1) + text("Partial") + struct.pack("<BH", 0, 1) + text("Unknown") + bytes([6, 0])
        for payload in (vmad()[:-1], variable):
            with self.subTest(payload=payload):
                result, output = self.run_dump([("ACTI", 0x800, payload, 0)])
                self.assertEqual(1, result.returncode, result.stderr)
                self.assertIn("000800:first.esp", (output / "papyrus_failures.tsv").read_text(encoding="utf-8"))

    def test_truncated_quest_extras_are_refused(self):
        result, output = self.run_dump([("QUST", 0x800, vmad() + bytes([0, 1]), 0)])
        self.assertEqual(1, result.returncode, result.stderr)
        self.assertIn("000800:first.esp", (output / "papyrus_failures.tsv").read_text(encoding="utf-8"))

    def test_an_output_failure_refuses_the_measurement(self):
        for filename in ("papyrus.tsv", "papyrus_fragments.tsv"):
            with self.subTest(filename=filename):
                result, _ = self.run_dump([("ACTI", 0x800, vmad(), 0)], block_output=filename)
                self.assertEqual(1, result.returncode, result.stderr)

    def test_missing_order_cannot_be_an_empty_measurement(self):
        with tempfile.TemporaryDirectory(prefix="radicle-order-") as directory:
            root = pathlib.Path(directory)
            for mode in ("papyrus", "versions"):
                with self.subTest(mode=mode):
                    output = root / mode
                    result = subprocess.run([str(EXE), mode, str(root / "missing.tsv"), str(output)],
                                            capture_output=True, text=True)
                    self.assertEqual(1, result.returncode, result.stderr)
                    self.assertFalse(output.exists())

    def test_malformed_order_rows_cannot_be_silently_dropped(self):
        with tempfile.TemporaryDirectory(prefix="radicle-order-") as directory:
            root = pathlib.Path(directory)
            order = root / "order.tsv"
            for index, row in enumerate(("missing separator", "0\t")):
                with self.subTest(row=row):
                    order.write_text(row + "\n", encoding="utf-8")
                    output = root / f"measurement-{index}"
                    result = subprocess.run([str(EXE), "papyrus", str(order), str(output)], capture_output=True, text=True)
                    self.assertEqual(1, result.returncode, result.stderr)
                    self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
