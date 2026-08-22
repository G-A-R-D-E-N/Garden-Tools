#!/usr/bin/env python3
"""Validate a Gamebryo/FO4 NIF stream against the CK NiStream wire contract.

This is deliberately a wire validator, not a full class-payload parser. It
checks the structures that NiStream writes before/after the per-object
SaveBinary payloads:

  header line/version/endian/user/roots/BS version
  four BS header strings
  block-type CString table and root type indices
  root object-size table and payload bounds
  fixed-string pool and max-length field
  object-group table
  trailing link-count/link-ID table

The object payloads are treated as opaque bytes, but their declared sizes must
partition the stream exactly up to the trailing link table.
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

EXPECTED_HEADER = b"Gamebryo File Format, Version 20.2.0.7\n"
EXPECTED_VERSION = 0x14020007
MAX_COUNT = 1_000_000
MAX_STRING = 16 * 1024 * 1024
NULL_LINK_ID = 0xFFFFFFFF


class NifFormatError(ValueError):
    """A structural or semantic NIF validation failure."""


class Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.offset = 0

    def need(self, size: int, what: str) -> None:
        if size < 0 or self.offset + size > len(self.data):
            raise NifFormatError(
                f"truncated {what} at 0x{self.offset:x}: "
                f"need {size} bytes, have {len(self.data) - self.offset}"
            )

    def bytes(self, size: int, what: str) -> bytes:
        self.need(size, what)
        value = self.data[self.offset : self.offset + size]
        self.offset += size
        return value

    def u8(self, what: str) -> int:
        return self.bytes(1, what)[0]

    def u16(self, what: str) -> int:
        return struct.unpack("<H", self.bytes(2, what))[0]

    def u32(self, what: str) -> int:
        return struct.unpack("<I", self.bytes(4, what))[0]

    def count(self, value: int, what: str) -> int:
        if value > MAX_COUNT:
            raise NifFormatError(f"{what} is implausibly large: {value}")
        return value

    def cstring(self, what: str) -> bytes:
        size = self.u32(f"{what} length")
        if size > MAX_STRING:
            raise NifFormatError(f"{what} length is implausibly large: {size}")
        return self.bytes(size, what)


@dataclass
class NifReport:
    path: str
    file_size: int
    header_size: int
    file_version: int
    endian: int
    user_version: int
    root_count: int
    bs_version: int
    bs_strings: list[str]
    block_type_count: int
    block_types: list[str]
    streamable_root_count: int
    type_indices: list[int]
    object_sizes: list[int]
    fixed_string_count: int
    fixed_string_max_length: int
    fixed_string_actual_max: int
    group_count: int
    groups: list[int]
    payload_offset: int
    payload_size: int
    payload_end: int
    link_count: int
    link_ids: list[int]
    null_link_count: int
    warnings: list[str]

    def to_dict(self) -> dict[str, object]:
        return asdict(self)


def decode(value: bytes) -> str:
    return value.rstrip(b"\0").decode("utf-8", "replace")


def parse_nif(path: Path, *, strict_version: bool = True) -> NifReport:
    data = path.read_bytes()
    reader = Reader(data)
    warnings: list[str] = []

    # PutS writes the complete line without a length prefix. Do not assume the
    # stale 31-byte figure found in older notes; the literal is 39 bytes.
    header_end = data.find(b"\n")
    if header_end < 0:
        raise NifFormatError("missing NIF header newline")
    header = reader.bytes(header_end + 1, "header line")
    if header != EXPECTED_HEADER:
        raise NifFormatError(
            f"unexpected header line {header!r}; expected {EXPECTED_HEADER!r}"
        )

    file_version = reader.u32("file version")
    endian = reader.u8("endian flag")
    user_version = reader.u32("user version")
    root_count = reader.count(reader.u32("root count"), "root count")
    bs_version = reader.u32("BS version")

    if strict_version and file_version != EXPECTED_VERSION:
        raise NifFormatError(
            f"unsupported file version 0x{file_version:08x}; "
            f"expected 0x{EXPECTED_VERSION:08x}"
        )
    if endian != 1:
        raise NifFormatError(f"unsupported endian flag {endian}; expected 1")
    if user_version > 12:
        warnings.append(f"user version {user_version} is newer than CK max 12")

    bs_strings: list[str] = []
    for index in range(4):
        size = reader.u8(f"BS header string {index} length")
        raw = reader.bytes(size, f"BS header string {index}")
        if size and not raw.endswith(b"\0"):
            warnings.append(f"BS header string {index} length does not include NUL")
        bs_strings.append(decode(raw))

    block_type_count = reader.u16("block-type count")
    reader.count(block_type_count, "block-type count")
    block_types = [
        decode(reader.cstring(f"block type {index}"))
        for index in range(block_type_count)
    ]
    if len(set(block_types)) != len(block_types):
        raise NifFormatError("block-type table contains duplicate names")
    if any(not name for name in block_types):
        raise NifFormatError("block-type table contains an empty name")

    type_words = [reader.u16(f"root {index} type index") for index in range(root_count)]
    type_indices: list[int] = []
    streamable_root_count = 0
    for index, word in enumerate(type_words):
        if word & 0x8000:
            streamable_root_count += 1
        type_index = word & 0x7FFF
        if type_index >= block_type_count:
            raise NifFormatError(
                f"root {index} type index {type_index} exceeds "
                f"block-type count {block_type_count}"
            )
        type_indices.append(type_index)

    object_sizes = [reader.u32(f"root {index} object size") for index in range(root_count)]

    fixed_string_count = reader.count(reader.u32("fixed-string count"), "fixed-string count")
    fixed_string_max_length = reader.u32("fixed-string maximum length")
    fixed_strings: list[bytes] = []
    for index in range(fixed_string_count):
        fixed_strings.append(reader.cstring(f"fixed string {index}"))
    actual_max = max((len(value) for value in fixed_strings), default=0)
    if actual_max > fixed_string_max_length:
        raise NifFormatError(
            f"fixed-string maximum {fixed_string_max_length} is below actual "
            f"maximum {actual_max}"
        )

    group_field = reader.u32("object-group count-minus-one")
    if group_field == NULL_LINK_ID:
        group_count = 0
    else:
        group_count = reader.count(group_field + 1, "object-group count")
    groups = [reader.u32(f"object group {index}") for index in range(group_count)]

    payload_offset = reader.offset
    payload_size = sum(object_sizes)
    reader.need(payload_size, "root object payloads")
    payload_end = reader.offset + payload_size
    reader.offset = payload_end

    link_count = reader.count(reader.u32("link count"), "link count")
    link_ids = [reader.u32(f"link ID {index}") for index in range(link_count)]
    for index, link_id in enumerate(link_ids):
        if link_id != NULL_LINK_ID and link_id >= link_count:
            raise NifFormatError(
                f"link ID {index} = {link_id} is outside link table of "
                f"{link_count} entries"
            )
    if reader.offset != len(data):
        raise NifFormatError(
            f"{len(data) - reader.offset} trailing bytes after link table at "
            f"0x{reader.offset:x}"
        )

    return NifReport(
        path=str(path),
        file_size=len(data),
        header_size=header_end + 1,
        file_version=file_version,
        endian=endian,
        user_version=user_version,
        root_count=root_count,
        bs_version=bs_version,
        bs_strings=bs_strings,
        block_type_count=block_type_count,
        block_types=block_types,
        streamable_root_count=streamable_root_count,
        type_indices=type_indices,
        object_sizes=object_sizes,
        fixed_string_count=fixed_string_count,
        fixed_string_max_length=fixed_string_max_length,
        fixed_string_actual_max=actual_max,
        group_count=group_count,
        groups=groups,
        payload_offset=payload_offset,
        payload_size=payload_size,
        payload_end=payload_end,
        link_count=link_count,
        link_ids=link_ids,
        null_link_count=sum(link_id == NULL_LINK_ID for link_id in link_ids),
        warnings=warnings,
    )


def print_report(report: NifReport) -> None:
    print(f"PASS {report.path}")
    print(
        f"  version=0x{report.file_version:08x} endian={report.endian} "
        f"user={report.user_version} bs=0x{report.bs_version:x}"
    )
    print(
        f"  roots={report.root_count} streamable={report.streamable_root_count} "
        f"block_types={report.block_type_count}"
    )
    print(
        f"  fixed_strings={report.fixed_string_count} "
        f"max={report.fixed_string_max_length} "
        f"actual_max={report.fixed_string_actual_max}"
    )
    print(
        f"  groups={report.group_count} payload=0x{report.payload_offset:x}.."
        f"0x{report.payload_end:x} ({report.payload_size} bytes)"
    )
    print(f"  links={report.link_count} null_links={report.null_link_count}")
    print("  block types: " + ", ".join(report.block_types))
    if report.warnings:
        for warning in report.warnings:
            print(f"  warning: {warning}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("nif", type=Path, nargs="+", help="NIF files to validate")
    parser.add_argument(
        "--allow-version",
        action="store_true",
        help="accept versions other than the CK-emitted 20.2.0.7",
    )
    parser.add_argument("--json", action="store_true", help="emit reports as JSON")
    args = parser.parse_args(argv)

    reports: list[dict[str, object]] = []
    failed = False
    for path in args.nif:
        try:
            report = parse_nif(path, strict_version=not args.allow_version)
        except (OSError, NifFormatError) as exc:
            failed = True
            print(f"FAIL {path}: {exc}", file=sys.stderr)
            continue
        if args.json:
            reports.append(report.to_dict())
        else:
            print_report(report)
    if args.json:
        print(json.dumps(reports, indent=2))
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
