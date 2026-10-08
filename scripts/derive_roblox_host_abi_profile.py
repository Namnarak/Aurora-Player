#!/usr/bin/env python3
# Copyright 2026 Aurora Project Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Build a HostAbi profile for one approved payload without running it.

The reference sidecar must match the reference ELF digest. The resulting
manifest is meant for an updater probation run; it does not mark the payload as
supported.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import mmap
import os
from pathlib import Path
import re
import stat
import struct
import sys
import tempfile
from typing import Any, Iterator, Sequence

try:
    import capstone
    from capstone import x86 as capstone_x86
except ImportError:
    capstone = None
    capstone_x86 = None


MAX_ELF_BYTES = 512 * 1024 * 1024
MAX_JSON_BYTES = 1024 * 1024
MAX_RELOCATIONS = 4 * 1024 * 1024
MAX_INIT_ARRAY_ENTRIES = 64 * 1024
SHA256_CHUNK_BYTES = 1024 * 1024
REGISTRY_INITIALIZER_MAX_BYTES = 0x1000
ELF_BUILD_ID_PATTERN = re.compile(r"^[0-9a-f]{40}$")
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
PAYLOAD_ID_PATTERN = re.compile(r"^[1-9][0-9]*-[0-9a-f]{40}$")
RVA_PATTERN = re.compile(r"^0x[1-9a-f][0-9a-f]*$")

ELF_HEADER = struct.Struct("<16sHHIQQQIHHHHHH")
PROGRAM_HEADER = struct.Struct("<IIQQQQQQ")
SECTION_HEADER = struct.Struct("<IIQQQQIIQQ")
SYMBOL_ENTRY = struct.Struct("<IBBHQQ")
RELA_ENTRY = struct.Struct("<QQq")
DYNAMIC_ENTRY = struct.Struct("<qQ")
NOTE_HEADER = struct.Struct("<III")

ELF_CLASS_64 = 2
ELF_DATA_LITTLE_ENDIAN = 1
ELF_VERSION_CURRENT = 1
ELF_TYPE_SHARED_OBJECT = 3
ELF_MACHINE_X86_64 = 62
PT_LOAD = 1
PT_DYNAMIC = 2
PT_GNU_RELRO = 0x6474E552
PF_EXECUTE = 1
PF_WRITE = 2
PF_READ = 4
SHF_WRITE = 1
SHF_ALLOC = 2
SHF_EXECINSTR = 4
SHT_PROGBITS = 1
SHT_RELA = 4
SHT_DYNAMIC = 6
SHT_STRTAB = 3
SHT_NOTE = 7
SHT_DYNSYM = 11
SHT_INIT_ARRAY = 14
SHT_ANDROID_RELA = 0x60000002
R_X86_64_RELATIVE = 8
R_X86_64_JUMP_SLOT = 7
DT_NULL = 0
DT_PLTRELSZ = 2
DT_PLTGOT = 3
DT_STRTAB = 5
DT_SYMTAB = 6
DT_RELA = 7
DT_RELASZ = 8
DT_RELAENT = 9
DT_STRSZ = 10
DT_SYMENT = 11
DT_PLTREL = 20
DT_JMPREL = 23
DT_ANDROID_REL = 0x6000000F
DT_ANDROID_RELSZ = 0x60000010
DT_ANDROID_RELA = 0x60000011
DT_ANDROID_RELASZ = 0x60000012
DT_ANDROID_RELR = 0x6FFFE000
DT_ANDROID_RELRSZ = 0x6FFFE001
DT_ANDROID_RELRENT = 0x6FFFE003
DT_REL = 17
DT_RELSZ = 18
DT_RELENT = 19
DT_RELRSZ = 35
DT_RELR = 36
DT_RELRENT = 37
DT_RELACOUNT = 0x6FFFFFF9
DT_RELCOUNT = 0x6FFFFFFA
UINT64_MAX = (1 << 64) - 1
INT64_MIN = -(1 << 63)
INT64_MAX = (1 << 63) - 1
MAX_DYNAMIC_TABLE_BYTES = 1024 * 1024

UNSUPPORTED_RELOCATION_TAGS = frozenset(
    {
        DT_RELA,
        DT_RELASZ,
        DT_REL,
        DT_RELSZ,
        DT_RELENT,
        DT_RELRSZ,
        DT_RELR,
        DT_RELRENT,
        DT_ANDROID_REL,
        DT_ANDROID_RELSZ,
        DT_ANDROID_RELR,
        DT_ANDROID_RELRSZ,
        DT_ANDROID_RELRENT,
        DT_RELACOUNT,
        DT_RELCOUNT,
    }
)
R_X86_64_64 = 1
R_X86_64_GLOB_DAT = 6

APS2_GROUPED_BY_INFO = 1
APS2_GROUPED_BY_OFFSET_DELTA = 2
APS2_GROUPED_BY_ADDEND = 4
APS2_GROUP_HAS_ADDEND = 8

REQUIRED_EXPORTS = frozenset(
    {
        "JNI_OnLoad",
        "Java_com_roblox_engine_jni_NativeGLInterface_nativeGameGlobalInit",
        "Java_com_roblox_engine_jni_NativeGLInterface_nativeUpdateAdapterInit",
        "Java_com_roblox_engine_jni_NativeGLInterface_nativeAppBridgeV2InitWithParams",
        "Java_com_roblox_engine_jni_NativeGLInterface_nativeAppBridgeStartLuaAppDM",
        "Java_com_roblox_engine_jni_NativeGLInterface_nativeAppBridgeV2StartAppWithParams",
        "Java_com_roblox_engine_jni_NativeGLInterface_nativeCallMessagesFromMainThread",
    }
)

PRIMARY_TOOLCHAIN_MARKERS = (
    b"Android (13624864, +pgo, +bolt, +lto, +mlgo, based on r530567e) "
    b"clang version 19.0.1",
    b"Linker: LLD 19.0.1",
)


class AnalyzerError(RuntimeError):
    """Raised when HostAbi analysis rejects an input."""


@dataclass(frozen=True)
class Segment:
    offset: int
    virtual_address: int
    file_size: int
    memory_size: int
    flags: int

    def contains_memory(self, rva: int, size: int = 1) -> bool:
        return (
            size >= 0
            and self.virtual_address <= rva
            and rva + size <= self.virtual_address + self.memory_size
        )

    def contains_file_data(self, rva: int, size: int = 1) -> bool:
        return (
            size >= 0
            and self.virtual_address <= rva
            and rva + size <= self.virtual_address + self.file_size
        )


@dataclass(frozen=True)
class Section:
    index: int
    name: str
    section_type: int
    flags: int
    address: int
    offset: int
    size: int
    link: int
    info: int
    entry_size: int


@dataclass(frozen=True)
class Relocation:
    offset: int
    info: int
    addend: int


@dataclass(frozen=True)
class SignatureSpec:
    name: str
    reference_rva: int
    instruction_count: int
    allow_registry_object_size_change: bool = False
    minimum_anchor_bytes: int = 6
    registry_size_indices: tuple[int, int] = (8, 12)


@dataclass(frozen=True)
class SignatureMatch:
    rva: int
    reference_instructions: tuple[Any, ...]
    candidate_instructions: tuple[Any, ...]


class Sleb128Reader:
    def __init__(self, encoded: bytes):
        self._encoded = encoded
        self.position = 0

    def pop(self) -> int:
        value = 0
        shift = 0
        while True:
            if self.position >= len(self._encoded) or shift > 63:
                raise AnalyzerError("invalid or truncated APS2 SLEB128 value")
            byte = self._encoded[self.position]
            self.position += 1
            payload = byte & 0x7F
            if shift == 63:
                if byte & 0x80 or payload not in (0, 0x7F):
                    raise AnalyzerError(
                        "APS2 SLEB128 value exceeds signed 64-bit bounds"
                    )
                value |= (payload & 1) << 63
                break
            value |= payload << shift
            shift += 7
            if not byte & 0x80:
                if byte & 0x40:
                    value -= 1 << shift
                break
        if value >= 1 << 63:
            value -= 1 << 64
        if not INT64_MIN <= value <= INT64_MAX:
            raise AnalyzerError("APS2 SLEB128 value exceeds 64-bit bounds")
        return value

    def at_end(self) -> bool:
        return self.position == len(self._encoded)


def sha256_file(file_object: Any) -> str:
    digest = hashlib.sha256()
    while True:
        try:
            chunk = file_object.read(SHA256_CHUNK_BYTES)
        except OSError as error:
            raise AnalyzerError("cannot hash ELF file") from error
        if not chunk:
            return digest.hexdigest()
        digest.update(chunk)


class ElfImage:
    def __init__(self, path: Path):
        self.path = path
        self._file = None
        self.data = None
        self.file_size = 0
        self.sha256 = ""
        self.sections: dict[str, Section] = {}
        self.sections_by_index: tuple[Section, ...] = ()
        self.segments: tuple[Segment, ...] = ()
        self.dynamic_segments: tuple[Segment, ...] = ()
        self.relro_ranges: tuple[tuple[int, int], ...] = ()
        self.build_id = ""

    def __enter__(self) -> "ElfImage":
        try:
            file_status = os.lstat(self.path)
        except OSError as error:
            raise AnalyzerError(f"cannot stat ELF file: {self.path}") from error
        if stat.S_ISLNK(file_status.st_mode) or not stat.S_ISREG(file_status.st_mode):
            raise AnalyzerError(f"ELF input must be a regular non-symlink: {self.path}")
        if file_status.st_size < ELF_HEADER.size or file_status.st_size > MAX_ELF_BYTES:
            raise AnalyzerError(f"ELF input has an invalid size: {self.path}")
        self.file_size = file_status.st_size
        try:
            self._file = self.path.open("rb")
            self.sha256 = sha256_file(self._file)
            self._file.seek(0)
            self.data = mmap.mmap(self._file.fileno(), 0, access=mmap.ACCESS_READ)
            self._parse()
        except AnalyzerError:
            self.__exit__(None, None, None)
            raise
        except (OSError, ValueError, struct.error) as error:
            self.__exit__(None, None, None)
            raise AnalyzerError(f"cannot parse ELF file: {self.path}") from error
        return self

    def __exit__(self, _type, _value, _traceback) -> None:
        if self.data is not None:
            self.data.close()
            self.data = None
        if self._file is not None:
            self._file.close()
            self._file = None

    def _checked_range(self, offset: int, size: int, description: str) -> None:
        if offset < 0 or size < 0 or offset > self.file_size - size:
            raise AnalyzerError(f"ELF {description} exceeds file bounds")

    def bytes_at(self, offset: int, size: int, description: str) -> bytes:
        self._checked_range(offset, size, description)
        return self.data[offset : offset + size]

    def section_bytes(self, section: Section) -> bytes:
        return self.bytes_at(section.offset, section.size, f"section {section.name}")

    def _parse(self) -> None:
        header = ELF_HEADER.unpack_from(self.data, 0)
        identification = header[0]
        if (
            identification[:4] != b"\x7fELF"
            or identification[4] != ELF_CLASS_64
            or identification[5] != ELF_DATA_LITTLE_ENDIAN
            or identification[6] != ELF_VERSION_CURRENT
            or header[1] != ELF_TYPE_SHARED_OBJECT
            or header[2] != ELF_MACHINE_X86_64
            or header[3] != ELF_VERSION_CURRENT
        ):
            raise AnalyzerError("candidate must be a little-endian x86-64 ET_DYN ELF")

        program_offset = header[5]
        section_offset = header[6]
        program_entry_size = header[9]
        program_count = header[10]
        section_entry_size = header[11]
        section_count = header[12]
        section_names_index = header[13]
        if (
            program_entry_size != PROGRAM_HEADER.size
            or section_entry_size != SECTION_HEADER.size
            or program_count == 0
            or section_count == 0
            or section_names_index == 0
            or section_names_index >= section_count
        ):
            raise AnalyzerError("ELF header tables use an unsupported layout")
        self._checked_range(
            program_offset, program_count * program_entry_size, "program headers"
        )
        self._checked_range(
            section_offset, section_count * section_entry_size, "section headers"
        )

        segments = []
        dynamic_segments = []
        relro_ranges = []
        for index in range(program_count):
            values = PROGRAM_HEADER.unpack_from(
                self.data, program_offset + index * program_entry_size
            )
            if values[0] == PT_GNU_RELRO:
                if values[6] <= 0:
                    raise AnalyzerError("ELF PT_GNU_RELRO range is empty")
                relro_ranges.append((values[3], values[6]))
            if values[0] == PT_DYNAMIC:
                dynamic_segment = Segment(
                    offset=values[2],
                    virtual_address=values[3],
                    file_size=values[5],
                    memory_size=values[6],
                    flags=values[1],
                )
                if (
                    dynamic_segment.file_size <= 0
                    or dynamic_segment.file_size != dynamic_segment.memory_size
                    or dynamic_segment.file_size > MAX_DYNAMIC_TABLE_BYTES
                    or dynamic_segment.file_size % DYNAMIC_ENTRY.size
                    or dynamic_segment.virtual_address
                    > UINT64_MAX - dynamic_segment.file_size
                ):
                    raise AnalyzerError(
                        "ELF PT_DYNAMIC is not a bounded file-backed table"
                    )
                self._checked_range(
                    dynamic_segment.offset,
                    dynamic_segment.file_size,
                    "PT_DYNAMIC data",
                )
                dynamic_segments.append(dynamic_segment)
            if values[0] != PT_LOAD:
                continue
            segment = Segment(
                offset=values[2],
                virtual_address=values[3],
                file_size=values[5],
                memory_size=values[6],
                flags=values[1],
            )
            self._checked_range(segment.offset, segment.file_size, "PT_LOAD data")
            if segment.file_size > segment.memory_size:
                raise AnalyzerError("ELF PT_LOAD file size exceeds memory size")
            segments.append(segment)
        if not segments:
            raise AnalyzerError("ELF has no PT_LOAD segments")
        self.segments = tuple(segments)
        self.dynamic_segments = tuple(dynamic_segments)
        self.relro_ranges = tuple(relro_ranges)

        raw_sections = [
            SECTION_HEADER.unpack_from(
                self.data, section_offset + index * section_entry_size
            )
            for index in range(section_count)
        ]
        names_header = raw_sections[section_names_index]
        self._checked_range(names_header[4], names_header[5], "section-name table")
        names = self.data[names_header[4] : names_header[4] + names_header[5]]

        parsed_sections = []
        by_name = {}
        for index, values in enumerate(raw_sections):
            name_offset = values[0]
            if name_offset >= len(names):
                raise AnalyzerError("ELF section name exceeds string table")
            name_end = names.find(b"\0", name_offset)
            if name_end < 0:
                raise AnalyzerError("ELF section name is not terminated")
            try:
                name = names[name_offset:name_end].decode("ascii")
            except UnicodeDecodeError as error:
                raise AnalyzerError("ELF section name is not ASCII") from error
            section = Section(
                index=index,
                name=name,
                section_type=values[1],
                flags=values[2],
                address=values[3],
                offset=values[4],
                size=values[5],
                link=values[6],
                info=values[7],
                entry_size=values[9],
            )
            if section.section_type != 8:  # SHT_NOBITS has no file bytes.
                self._checked_range(section.offset, section.size, f"section {name}")
            if name and name in by_name:
                raise AnalyzerError(f"ELF contains duplicate section {name}")
            if name:
                by_name[name] = section
            parsed_sections.append(section)
        self.sections = by_name
        self.sections_by_index = tuple(parsed_sections)

        self._validate_required_sections()
        self.build_id = self._read_build_id()

    def _require_section(
        self, name: str, section_type: int, required_flags: int = 0
    ) -> Section:
        section = self.sections.get(name)
        if section is None or section.section_type != section_type:
            raise AnalyzerError(f"ELF is missing required {name} section")
        if section.flags & required_flags != required_flags:
            raise AnalyzerError(f"ELF {name} section has invalid flags")
        return section

    def _validate_required_sections(self) -> None:
        text = self._require_section(".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR)
        if text.flags & SHF_WRITE:
            raise AnalyzerError("ELF .text section is writable")
        init_array = self._require_section(
            ".init_array", SHT_INIT_ARRAY, SHF_ALLOC | SHF_WRITE
        )
        if init_array.flags & SHF_EXECINSTR or init_array.entry_size not in (0, 8):
            raise AnalyzerError("ELF .init_array section has invalid flags or stride")
        if (
            init_array.size == 0
            or init_array.size % 8 != 0
            or init_array.size // 8 > MAX_INIT_ARRAY_ENTRIES
        ):
            raise AnalyzerError("ELF .init_array size is invalid")
        rela_dyn = self._require_section(".rela.dyn", SHT_ANDROID_RELA, SHF_ALLOC)
        dynsym = self._require_section(".dynsym", SHT_DYNSYM, SHF_ALLOC)
        dynstr = self._require_section(".dynstr", SHT_STRTAB, SHF_ALLOC)
        self._require_section(".note.gnu.build-id", SHT_NOTE, SHF_ALLOC)
        self._require_section(".comment", SHT_PROGBITS)
        if (
            dynsym.entry_size != SYMBOL_ENTRY.size
            or dynsym.size % SYMBOL_ENTRY.size
        ):
            raise AnalyzerError("ELF .dynsym entry size or size is invalid")
        self.require_code_rva(text.address, text.size)
        self.require_writable_rva(init_array.address, init_array.size)
        self._validate_global_dynamic_bindings(rela_dyn, dynsym, dynstr)

    def _read_build_id(self) -> str:
        section = self.sections[".note.gnu.build-id"]
        encoded = self.section_bytes(section)
        offset = 0
        matches = []
        while offset < len(encoded):
            if len(encoded) - offset < NOTE_HEADER.size:
                raise AnalyzerError("GNU Build ID note is truncated")
            name_size, descriptor_size, note_type = NOTE_HEADER.unpack_from(
                encoded, offset
            )
            offset += NOTE_HEADER.size
            name_end = offset + name_size
            descriptor_offset = (name_end + 3) & ~3
            descriptor_end = descriptor_offset + descriptor_size
            next_note = (descriptor_end + 3) & ~3
            if name_end > len(encoded) or next_note > len(encoded):
                raise AnalyzerError("GNU Build ID note exceeds section bounds")
            name = encoded[offset:name_end]
            descriptor = encoded[descriptor_offset:descriptor_end]
            if name == b"GNU\0" and note_type == 3:
                matches.append(descriptor)
            offset = next_note
        if len(matches) != 1 or len(matches[0]) != 20:
            raise AnalyzerError("ELF must contain one 20-byte GNU Build ID")
        return matches[0].hex()

    def segment_for_rva(self, rva: int, size: int = 1) -> Segment | None:
        matches = [
            segment for segment in self.segments if segment.contains_memory(rva, size)
        ]
        return matches[0] if len(matches) == 1 else None

    def require_code_rva(self, rva: int, size: int = 1) -> None:
        segment = self.segment_for_rva(rva, size)
        if (
            segment is None
            or segment.flags & (PF_READ | PF_EXECUTE) != (PF_READ | PF_EXECUTE)
            or segment.flags & PF_WRITE
            or not segment.contains_file_data(rva, size)
        ):
            raise AnalyzerError(f"RVA 0x{rva:x} is not in a read/execute segment")

    def require_writable_rva(self, rva: int, size: int = 1) -> None:
        segment = self.segment_for_rva(rva, size)
        if (
            segment is None
            or segment.flags & (PF_READ | PF_WRITE) != (PF_READ | PF_WRITE)
            or segment.flags & PF_EXECUTE
        ):
            raise AnalyzerError(f"RVA 0x{rva:x} is not in a non-executable RW segment")

    def require_relro_rva(self, rva: int, size: int = 1) -> None:
        matches = [
            (begin, length)
            for begin, length in self.relro_ranges
            if size >= 0 and begin <= rva and rva + size <= begin + length
        ]
        if len(matches) != 1:
            raise AnalyzerError(
                f"RVA 0x{rva:x} is not in a unique PT_GNU_RELRO range"
            )

    def rva_to_offset(self, rva: int, size: int = 1) -> int:
        matches = [
            segment
            for segment in self.segments
            if segment.contains_file_data(rva, size)
        ]
        if len(matches) != 1:
            raise AnalyzerError(f"RVA 0x{rva:x} has no unique file mapping")
        segment = matches[0]
        offset = segment.offset + rva - segment.virtual_address
        self._checked_range(offset, size, "RVA mapping")
        return offset

    def validate_toolchain(self) -> None:
        comment = self.section_bytes(self.sections[".comment"])
        for marker in PRIMARY_TOOLCHAIN_MARKERS:
            if marker not in comment:
                raise AnalyzerError(
                    "ELF compiler/linker family differs from the approved reference"
                )

    def exported_functions(self) -> dict[str, int]:
        dynsym = self.sections[".dynsym"]
        if dynsym.link >= len(self.sections_by_index):
            raise AnalyzerError("ELF .dynsym has an invalid string-table link")
        dynstr = self.sections_by_index[dynsym.link]
        if dynstr.name != ".dynstr" or dynstr.section_type != SHT_STRTAB:
            raise AnalyzerError("ELF .dynsym does not reference .dynstr")
        strings = self.section_bytes(dynstr)
        symbols = self.section_bytes(dynsym)
        if len(symbols) % SYMBOL_ENTRY.size != 0:
            raise AnalyzerError("ELF .dynsym has a truncated entry")
        result: dict[str, int] = {}
        for offset in range(0, len(symbols), SYMBOL_ENTRY.size):
            name_offset, info, _other, section_index, value, size = (
                SYMBOL_ENTRY.unpack_from(symbols, offset)
            )
            if name_offset >= len(strings):
                raise AnalyzerError("ELF dynamic symbol name exceeds .dynstr")
            name_end = strings.find(b"\0", name_offset)
            if name_end < 0:
                raise AnalyzerError("ELF dynamic symbol name is not terminated")
            symbol_type = info & 0x0F
            symbol_binding = info >> 4
            if symbol_type != 2 or symbol_binding not in (1, 2) or section_index == 0:
                continue
            try:
                name = strings[name_offset:name_end].decode("ascii")
            except UnicodeDecodeError as error:
                raise AnalyzerError("ELF dynamic symbol name is not ASCII") from error
            if not name:
                continue
            self.require_code_rva(value, max(1, min(size, 16)))
            if name in result and result[name] != value:
                raise AnalyzerError(f"ELF contains ambiguous export {name}")
            result[name] = value
        return result

    def _require_unique_load_file_mapping(
        self, address: int, size: int, expected_offset: int, description: str
    ) -> None:
        if size <= 0:
            raise AnalyzerError(f"ELF {description} has an empty file-backed range")
        self._checked_range(expected_offset, size, description)
        matches = []
        for segment in self.segments:
            if address < segment.virtual_address:
                continue
            displacement = address - segment.virtual_address
            if (
                displacement > segment.file_size
                or size > segment.file_size - displacement
            ):
                continue
            mapped_offset = segment.offset + displacement
            if mapped_offset > UINT64_MAX:
                raise AnalyzerError(f"ELF {description} PT_LOAD offset overflows")
            matches.append(mapped_offset)
        if len(matches) != 1 or matches[0] != expected_offset:
            raise AnalyzerError(
                f"ELF {description} does not map uniquely to its exact PT_LOAD bytes"
            )

    def _validate_global_dynamic_bindings(
        self, rela_dyn: Section, dynsym: Section, dynstr: Section
    ) -> None:
        if len(self.dynamic_segments) != 1:
            raise AnalyzerError("ELF does not have one unique PT_DYNAMIC segment")
        dynamic = self.dynamic_segments[0]
        dynamic_section = self.sections.get(".dynamic")
        if (
            dynamic_section is None
            or dynamic_section.section_type != SHT_DYNAMIC
            or dynamic_section.flags & SHF_ALLOC == 0
            or dynamic_section.address != dynamic.virtual_address
            or dynamic_section.offset != dynamic.offset
            or dynamic_section.size != dynamic.file_size
            or dynamic_section.entry_size != DYNAMIC_ENTRY.size
            or dynamic_section.link != dynstr.index
        ):
            raise AnalyzerError("ELF PT_DYNAMIC does not match its exact .dynamic section")
        self._require_unique_load_file_mapping(
            dynamic.virtual_address,
            dynamic.file_size,
            dynamic.offset,
            "PT_DYNAMIC",
        )
        if (
            rela_dyn.section_type != SHT_ANDROID_RELA
            or rela_dyn.flags & SHF_ALLOC == 0
            or rela_dyn.link != dynsym.index
            or rela_dyn.info != 0
            or rela_dyn.entry_size != 1
        ):
            raise AnalyzerError("ELF .rela.dyn metadata is unsupported")
        self._require_unique_load_file_mapping(
            rela_dyn.address, rela_dyn.size, rela_dyn.offset, ".rela.dyn"
        )
        if (
            dynsym.section_type != SHT_DYNSYM
            or dynsym.flags & SHF_ALLOC == 0
            or dynsym.entry_size != SYMBOL_ENTRY.size
            or dynsym.size % SYMBOL_ENTRY.size
            or dynsym.link != dynstr.index
            or dynstr.section_type != SHT_STRTAB
            or dynstr.flags & SHF_ALLOC == 0
        ):
            raise AnalyzerError("ELF symbol and string table metadata is unsupported")
        self._require_unique_load_file_mapping(
            dynsym.address, dynsym.size, dynsym.offset, ".dynsym"
        )
        self._require_unique_load_file_mapping(
            dynstr.address, dynstr.size, dynstr.offset, ".dynstr"
        )

        dynamic_bytes = self.section_bytes(dynamic_section)
        required_tags = {
            DT_ANDROID_RELA,
            DT_ANDROID_RELASZ,
            DT_SYMTAB,
            DT_STRTAB,
            DT_SYMENT,
            DT_STRSZ,
        }
        values = {}
        terminated = False
        for offset in range(0, len(dynamic_bytes), DYNAMIC_ENTRY.size):
            tag, value = DYNAMIC_ENTRY.unpack_from(dynamic_bytes, offset)
            if terminated:
                if tag != DT_NULL:
                    raise AnalyzerError("PT_DYNAMIC has non-null data after DT_NULL")
                continue
            if tag == DT_NULL:
                terminated = True
                continue
            if tag in UNSUPPORTED_RELOCATION_TAGS:
                raise AnalyzerError(
                    "PT_DYNAMIC indicates an unsupported relocation table format"
                )
            if tag not in required_tags:
                continue
            if tag in values:
                raise AnalyzerError("PT_DYNAMIC has a duplicate required global tag")
            values[tag] = value
        if not terminated or values.keys() != required_tags:
            raise AnalyzerError("PT_DYNAMIC is missing a required global table tag")
        if (
            values[DT_ANDROID_RELA] != rela_dyn.address
            or values[DT_ANDROID_RELASZ] != rela_dyn.size
            or values[DT_SYMTAB] != dynsym.address
            or values[DT_STRTAB] != dynstr.address
            or values[DT_SYMENT] != SYMBOL_ENTRY.size
            or values[DT_STRSZ] != dynstr.size
        ):
            raise AnalyzerError("PT_DYNAMIC tags do not match the global dynamic tables")

    def _validate_dynamic_plt_bindings(
        self,
        rela_plt: Section,
        rela_dyn: Section,
        dynsym: Section,
        dynstr: Section,
        got_plt: Section,
    ) -> None:
        if len(self.dynamic_segments) != 1:
            raise AnalyzerError("ELF does not have one unique PT_DYNAMIC segment")
        dynamic = self.dynamic_segments[0]
        dynamic_section = self.sections.get(".dynamic")
        if (
            dynamic_section is None
            or dynamic_section.section_type != SHT_DYNAMIC
            or dynamic_section.flags & SHF_ALLOC == 0
            or dynamic_section.address != dynamic.virtual_address
            or dynamic_section.offset != dynamic.offset
            or dynamic_section.size != dynamic.file_size
            or dynamic_section.entry_size != DYNAMIC_ENTRY.size
            or dynamic_section.link != dynstr.index
        ):
            raise AnalyzerError("ELF PT_DYNAMIC does not match its exact .dynamic section")
        self._require_unique_load_file_mapping(
            dynamic.virtual_address,
            dynamic.file_size,
            dynamic.offset,
            "PT_DYNAMIC",
        )
        for section in (rela_plt, rela_dyn, dynsym, dynstr, got_plt):
            self._require_unique_load_file_mapping(
                section.address, section.size, section.offset, section.name
            )

        dynamic_bytes = self.section_bytes(dynamic_section)
        required_tags = {
            DT_PLTREL,
            DT_JMPREL,
            DT_PLTRELSZ,
            DT_SYMTAB,
            DT_STRTAB,
            DT_STRSZ,
            DT_SYMENT,
            DT_RELAENT,
            DT_PLTGOT,
            DT_ANDROID_RELA,
            DT_ANDROID_RELASZ,
        }
        values = {}
        terminated = False
        for offset in range(0, len(dynamic_bytes), DYNAMIC_ENTRY.size):
            tag, value = DYNAMIC_ENTRY.unpack_from(dynamic_bytes, offset)
            if terminated:
                if tag != DT_NULL:
                    raise AnalyzerError("PT_DYNAMIC has non-null data after DT_NULL")
                continue
            if tag == DT_NULL:
                terminated = True
                continue
            if tag in UNSUPPORTED_RELOCATION_TAGS:
                raise AnalyzerError(
                    "PT_DYNAMIC indicates an unsupported relocation table format"
                )
            if tag not in required_tags:
                continue
            if tag in values:
                raise AnalyzerError("PT_DYNAMIC has a duplicate required tag")
            values[tag] = value
        if not terminated or required_tags != values.keys():
            raise AnalyzerError("PT_DYNAMIC is missing a required PLT import tag")
        if (
            values[DT_PLTREL] != DT_RELA
            or values[DT_JMPREL] != rela_plt.address
            or values[DT_PLTRELSZ] != rela_plt.size
            or values[DT_SYMTAB] != dynsym.address
            or values[DT_STRTAB] != dynstr.address
            or values[DT_STRSZ] != dynstr.size
            or values[DT_SYMENT] != SYMBOL_ENTRY.size
            or values[DT_RELAENT] != RELA_ENTRY.size
            or values[DT_PLTGOT] != got_plt.address
            or values[DT_ANDROID_RELA] != rela_dyn.address
            or values[DT_ANDROID_RELASZ] != rela_dyn.size
        ):
            raise AnalyzerError(
                "PT_DYNAMIC relocation tags do not match their exact sections"
            )

    def jump_slot_for_imported_function(self, requested_name: str) -> tuple[int, int]:
        rela_plt = self.sections.get(".rela.plt")
        rela_dyn = self.sections.get(".rela.dyn")
        dynsym = self.sections.get(".dynsym")
        dynstr = self.sections.get(".dynstr")
        got_plt = self.sections.get(".got.plt")
        if (
            rela_plt is None
            or rela_plt.section_type != SHT_RELA
            or rela_plt.flags & SHF_ALLOC == 0
            or rela_dyn is None
            or rela_dyn.section_type != SHT_ANDROID_RELA
            or rela_dyn.flags & SHF_ALLOC == 0
            or dynsym is None
            or dynsym.section_type != SHT_DYNSYM
            or dynsym.flags & SHF_ALLOC == 0
            or rela_dyn.link != dynsym.index
            or rela_dyn.info != 0
            or rela_dyn.entry_size != 1
            or dynstr is None
            or dynstr.section_type != SHT_STRTAB
            or dynstr.flags & SHF_ALLOC == 0
            or got_plt is None
            or got_plt.section_type != SHT_PROGBITS
            or got_plt.flags & (SHF_ALLOC | SHF_WRITE) != (SHF_ALLOC | SHF_WRITE)
            or got_plt.flags & SHF_EXECINSTR
            or rela_plt.link != dynsym.index
            or rela_plt.info != got_plt.index
            or dynsym.link != dynstr.index
            or rela_plt.entry_size != RELA_ENTRY.size
            or rela_plt.size % RELA_ENTRY.size
            or dynsym.entry_size != SYMBOL_ENTRY.size
            or dynsym.size % SYMBOL_ENTRY.size
        ):
            raise AnalyzerError("ELF has no supported PLT relocation and symbol mapping")
        self._validate_dynamic_plt_bindings(
            rela_plt, rela_dyn, dynsym, dynstr, got_plt
        )
        relocations = self.section_bytes(rela_plt)
        symbols = self.section_bytes(dynsym)
        strings = self.section_bytes(dynstr)
        symbol_count = len(symbols) // SYMBOL_ENTRY.size
        matches = []
        relocation_destinations = []
        for relocation_index, offset in enumerate(
            range(0, len(relocations), RELA_ENTRY.size)
        ):
            slot, relocation_info, _addend = RELA_ENTRY.unpack_from(
                relocations, offset
            )
            relocation_destinations.append(slot)
            if relocation_info & 0xFFFFFFFF != R_X86_64_JUMP_SLOT:
                continue
            symbol_index = relocation_info >> 32
            if symbol_index >= symbol_count:
                raise AnalyzerError("PLT relocation has an invalid dynamic-symbol index")
            name_offset, info, _other, section_index, _value, _size = (
                SYMBOL_ENTRY.unpack_from(symbols, symbol_index * SYMBOL_ENTRY.size)
            )
            if name_offset >= len(strings):
                raise AnalyzerError("PLT dynamic-symbol name exceeds .dynstr")
            name_end = strings.find(b"\0", name_offset)
            if name_end < 0:
                raise AnalyzerError("PLT dynamic-symbol name is not terminated")
            try:
                name = strings[name_offset:name_end].decode("ascii")
            except UnicodeDecodeError as error:
                raise AnalyzerError("PLT dynamic-symbol name is not ASCII") from error
            if name != requested_name:
                continue
            symbol_type = info & 0x0F
            symbol_binding = info >> 4
            if (
                section_index != 0
                or symbol_type != 2
                or symbol_binding not in (1, 2)
                or slot < got_plt.address
                or slot > got_plt.address + got_plt.size - 8
                or slot % 8
            ):
                raise AnalyzerError("requested PLT relocation is not a bounded function import")
            self.require_writable_rva(slot, 8)
            matches.append((slot, relocation_index))
        if len(matches) != 1:
            raise AnalyzerError("ELF does not have one unique requested PLT function import")
        selected_slot, relocation_index = matches[0]
        if relocation_destinations.count(selected_slot) != 1:
            raise AnalyzerError("PLT import slot has an aliased relocation destination")
        self._validate_no_dynamic_relocation_alias(rela_dyn, selected_slot)
        return selected_slot, relocation_index

    def _validate_no_dynamic_relocation_alias(
        self, rela_dyn: Section, slot: int
    ) -> None:
        if slot > UINT64_MAX - 8:
            raise AnalyzerError("selected PLT import slot exceeds unsigned bounds")
        slot_end = slot + 8
        for relocation in decode_aps2_relocations(self.section_bytes(rela_dyn)):
            relocation_type = relocation.info & 0xFFFFFFFF
            if relocation_type not in (
                R_X86_64_64,
                R_X86_64_GLOB_DAT,
                R_X86_64_RELATIVE,
            ):
                raise AnalyzerError("ELF .rela.dyn has an unsupported relocation writer")
            if relocation.offset > UINT64_MAX - 8:
                raise AnalyzerError("ELF .rela.dyn relocation destination overflows")
            relocation_end = relocation.offset + 8
            if relocation.offset < slot_end and slot < relocation_end:
                raise AnalyzerError("ELF .rela.dyn aliases the selected PLT import slot")

    def init_array_relocations(self) -> tuple[int, ...]:
        init_array = self.sections[".init_array"]
        relocations = decode_aps2_relocations(
            self.section_bytes(self.sections[".rela.dyn"])
        )
        selected: dict[int, int] = {}
        end = init_array.address + init_array.size
        for relocation in relocations:
            if not init_array.address <= relocation.offset < end:
                continue
            if (
                relocation.offset % 8 != 0
                or relocation.info & 0xFFFFFFFF != R_X86_64_RELATIVE
                or relocation.info >> 32 != 0
            ):
                raise AnalyzerError(".init_array contains a non-relative relocation")
            index = (relocation.offset - init_array.address) // 8
            if index in selected:
                raise AnalyzerError(".init_array relocation is duplicated")
            self.require_code_rva(relocation.addend)
            selected[index] = relocation.addend
        count = init_array.size // 8
        if sorted(selected) != list(range(count)):
            raise AnalyzerError(".init_array is not fully covered by APS2 relocations")
        return tuple(selected[index] for index in range(count))


def decode_aps2_relocations(encoded: bytes) -> Iterator[Relocation]:
    if not encoded.startswith(b"APS2"):
        raise AnalyzerError(".rela.dyn does not use the required APS2 encoding")
    reader = Sleb128Reader(encoded[4:])
    relocation_count = reader.pop()
    relocation_offset = reader.pop()
    if (
        relocation_count < 0
        or relocation_count > MAX_RELOCATIONS
        or relocation_offset < 0
    ):
        raise AnalyzerError("APS2 relocation header is invalid")
    relocation_info = 0
    relocation_addend = 0
    emitted = 0
    while emitted < relocation_count:
        group_size = reader.pop()
        group_flags = reader.pop()
        if (
            group_size <= 0
            or group_size > relocation_count - emitted
            or group_flags < 0
            or group_flags & ~0x0F
        ):
            raise AnalyzerError("APS2 relocation group is invalid")
        offset_delta = reader.pop() if group_flags & APS2_GROUPED_BY_OFFSET_DELTA else 0
        if group_flags & APS2_GROUPED_BY_INFO:
            relocation_info = reader.pop()
        addend_mode = group_flags & (APS2_GROUP_HAS_ADDEND | APS2_GROUPED_BY_ADDEND)
        if addend_mode == (APS2_GROUP_HAS_ADDEND | APS2_GROUPED_BY_ADDEND):
            relocation_addend = checked_signed_64_add(
                relocation_addend, reader.pop(), "relocation addend"
            )
        elif addend_mode != APS2_GROUP_HAS_ADDEND:
            relocation_addend = 0
        for _index in range(group_size):
            relocation_offset = checked_signed_64_add(
                relocation_offset,
                offset_delta
                if group_flags & APS2_GROUPED_BY_OFFSET_DELTA
                else reader.pop(),
                "relocation offset",
            )
            if not group_flags & APS2_GROUPED_BY_INFO:
                relocation_info = reader.pop()
            if addend_mode == APS2_GROUP_HAS_ADDEND:
                relocation_addend = checked_signed_64_add(
                    relocation_addend, reader.pop(), "relocation addend"
                )
            if (
                relocation_offset < 0
                or relocation_info < 0
                or relocation_addend < 0
                or relocation_offset >= 1 << 64
                or relocation_info >= 1 << 64
                or relocation_addend >= 1 << 64
            ):
                raise AnalyzerError("APS2 relocation exceeds unsigned 64-bit bounds")
            emitted += 1
            yield Relocation(relocation_offset, relocation_info, relocation_addend)
    if not reader.at_end():
        raise AnalyzerError("APS2 relocation stream contains trailing bytes")


def checked_signed_64_add(left: int, right: int, description: str) -> int:
    result = left + right
    if not INT64_MIN <= result <= INT64_MAX:
        raise AnalyzerError(f"APS2 {description} exceeds signed 64-bit bounds")
    return result


def require_capstone() -> None:
    if capstone is None or capstone_x86 is None:
        raise AnalyzerError(
            "Python capstone 5 is required for fail-closed HostAbi analysis"
        )
    try:
        version = capstone.cs_version()
    except (AttributeError, TypeError, ValueError) as error:
        raise AnalyzerError("Python capstone version cannot be verified") from error
    if (
        not isinstance(version, tuple)
        or len(version) < 2
        or not all(isinstance(component, int) for component in version[:2])
        or version[0] != 5
    ):
        raise AnalyzerError("Python capstone major version 5 is required")


def parse_rva(value: Any, description: str) -> int:
    if not isinstance(value, str) or RVA_PATTERN.fullmatch(value) is None:
        raise AnalyzerError(f"{description} must be a canonical nonzero hex RVA")
    return int(value, 16)


def format_rva(value: int) -> str:
    if value <= 0:
        raise AnalyzerError("cannot encode a zero or negative RVA")
    return f"0x{value:x}"


def read_json_object(path: Path, description: str) -> dict[str, Any]:
    try:
        file_status = os.lstat(path)
    except OSError as error:
        raise AnalyzerError(f"cannot stat {description}: {path}") from error
    if stat.S_ISLNK(file_status.st_mode) or not stat.S_ISREG(file_status.st_mode):
        raise AnalyzerError(f"{description} must be a regular non-symlink")
    if file_status.st_size <= 0 or file_status.st_size > MAX_JSON_BYTES:
        raise AnalyzerError(f"{description} has an invalid size")
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise AnalyzerError(f"cannot parse {description}") from error
    if not isinstance(value, dict):
        raise AnalyzerError(f"{description} must contain one JSON object")
    return value


def require_exact_keys(
    value: dict[str, Any], expected: set[str], description: str
) -> None:
    if set(value) != expected:
        raise AnalyzerError(f"{description} has unknown or missing fields")


def validated_sidecar(path: Path) -> dict[str, Any]:
    sidecar = read_json_object(path, "reference HostAbi sidecar")
    require_exact_keys(
        sidecar,
        {
            "schema_version",
            "elf_build_id",
            "payload_sha256",
            "payload_id",
            "payload_path",
            "reference",
            "profile",
            "derivation_anchors",
        },
        "reference HostAbi sidecar",
    )
    build_id = sidecar.get("elf_build_id")
    payload_sha256 = sidecar.get("payload_sha256")
    payload_id = sidecar.get("payload_id")
    payload_path = sidecar.get("payload_path")
    profile = sidecar.get("profile")
    anchors = sidecar.get("derivation_anchors")
    reference = sidecar.get("reference")
    if sidecar.get("schema_version") != 1:
        raise AnalyzerError("reference HostAbi sidecar schema is unsupported")
    if (
        not isinstance(build_id, str)
        or ELF_BUILD_ID_PATTERN.fullmatch(build_id) is None
    ):
        raise AnalyzerError("reference sidecar ELF Build ID is invalid")
    if (
        not isinstance(payload_sha256, str)
        or SHA256_PATTERN.fullmatch(payload_sha256) is None
    ):
        raise AnalyzerError("reference sidecar payload SHA-256 is invalid")
    if (
        not isinstance(payload_id, str)
        or PAYLOAD_ID_PATTERN.fullmatch(payload_id) is None
    ):
        raise AnalyzerError("reference sidecar payload ID is invalid")
    if (
        payload_id.split("-", 1)[1] != build_id
        or payload_path != f"payloads/{payload_id}"
    ):
        raise AnalyzerError("reference sidecar payload identity is inconsistent")
    if not isinstance(profile, dict) or profile.get("elf_build_id") != build_id:
        raise AnalyzerError("reference sidecar profile identity is inconsistent")
    if not isinstance(anchors, dict) or anchors.get("signature_version") != 1:
        raise AnalyzerError("reference sidecar derivation anchors are unavailable")
    if not isinstance(reference, dict):
        raise AnalyzerError("reference sidecar provenance is unavailable")
    require_exact_keys(
        reference,
        {"elf_build_id", "payload_sha256"},
        "reference sidecar provenance",
    )
    if (
        not isinstance(reference.get("elf_build_id"), str)
        or ELF_BUILD_ID_PATTERN.fullmatch(reference["elf_build_id"]) is None
        or not isinstance(reference.get("payload_sha256"), str)
        or SHA256_PATTERN.fullmatch(reference["payload_sha256"]) is None
        or reference["elf_build_id"] == build_id
        or reference["payload_sha256"] == payload_sha256
    ):
        raise AnalyzerError("reference sidecar provenance identity is invalid")
    validate_profile_shape(profile, anchors)
    return sidecar


def require_positive_integer(value: Any, description: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
        raise AnalyzerError(f"{description} must be a positive integer")
    return value


def validated_ranges(value: Any, description: str, count: int) -> list[dict[str, int]]:
    if not isinstance(value, list) or not value:
        raise AnalyzerError(f"{description} must contain constructor ranges")
    previous_end = 0
    result = []
    for item in value:
        if not isinstance(item, dict):
            raise AnalyzerError(f"{description} contains a non-object range")
        require_exact_keys(item, {"begin", "end_exclusive"}, description)
        begin = item.get("begin")
        end = item.get("end_exclusive")
        if (
            not isinstance(begin, int)
            or isinstance(begin, bool)
            or not isinstance(end, int)
            or isinstance(end, bool)
            or begin < 0
            or begin >= end
            or end > count
            or begin < previous_end
        ):
            raise AnalyzerError(f"{description} contains an invalid range")
        previous_end = end
        result.append({"begin": begin, "end_exclusive": end})
    return result


def validate_profile_shape(profile: dict[str, Any], anchors: dict[str, Any]) -> None:
    require_exact_keys(
        profile,
        {
            "elf_build_id",
            "bridge_entries",
            "data_seeds",
            "native_allocator",
            "init_array_offset",
            "init_array_count",
            "constructor_run_ranges",
            "native_mimalloc_constructor_run_ranges",
            "native_mimalloc_thread_initializer_after_constructor",
            "native_pre_jni_bootstrap",
            "default_allocator_strategy",
        },
        "reference profile",
    )
    require_exact_keys(
        anchors,
        {
            "signature_version",
            "allocator_object_initializer_rva",
            "empty_string_initializer_rva",
            "jni_singleton_initializer_rva",
            "constructor_rvas",
        },
        "reference derivation anchors",
    )
    bridge_entries = profile.get("bridge_entries")
    if not isinstance(bridge_entries, list) or len(bridge_entries) != 6:
        raise AnalyzerError("reference profile must contain six allocator bridges")
    required_bridge_labels = {
        "small-allocate": "allocate",
        "usable-size": "usable_size",
        "reallocate": "reallocate",
        "allocate": "allocate",
        "aligned-allocate-direct": "aligned_allocate",
        "free": "free",
    }
    seen_labels = set()
    seen_rvas = set()
    for entry in bridge_entries:
        if not isinstance(entry, dict):
            raise AnalyzerError("reference bridge entry must be an object")
        require_exact_keys(entry, {"rva", "kind", "label"}, "reference bridge")
        label = entry.get("label")
        if (
            label not in required_bridge_labels
            or entry.get("kind") != required_bridge_labels[label]
        ):
            raise AnalyzerError("reference bridge entry kind/label is invalid")
        rva = parse_rva(entry.get("rva"), f"bridge {label}")
        seen_labels.add(label)
        seen_rvas.add(rva)
    if seen_labels != set(required_bridge_labels):
        raise AnalyzerError("reference bridge labels are incomplete")
    if len(seen_rvas) != len(bridge_entries):
        raise AnalyzerError("reference bridge RVAs are not unique")

    data_seeds = profile.get("data_seeds")
    if not isinstance(data_seeds, dict):
        raise AnalyzerError("reference data seeds are unavailable")
    require_exact_keys(
        data_seeds,
        {
            "allocator_object_slot",
            "empty_string_slot",
            "jni_singleton_slot",
            "jni_singleton_bytes",
            "arena_initializer",
            "allocator_thread_initializer",
            "arena_guard_slot",
            "arena_table_slot",
            "arena_table_slot_count",
        },
        "reference data seeds",
    )
    for name in (
        "allocator_object_slot",
        "empty_string_slot",
        "jni_singleton_slot",
        "arena_initializer",
        "allocator_thread_initializer",
        "arena_guard_slot",
        "arena_table_slot",
    ):
        parse_rva(data_seeds.get(name), f"data seed {name}")
    jni_singleton_bytes = data_seeds.get("jni_singleton_bytes")
    if (
        not isinstance(jni_singleton_bytes, int)
        or isinstance(jni_singleton_bytes, bool)
        or jni_singleton_bytes not in (0, 0x400)
    ):
        raise AnalyzerError("JNI singleton size must be 0 or the legacy 1024-byte seed")
    require_positive_integer(
        data_seeds.get("arena_table_slot_count"), "arena slot count"
    )

    native_allocator = profile.get("native_allocator")
    if not isinstance(native_allocator, dict):
        raise AnalyzerError("reference native allocator is unavailable")
    require_exact_keys(
        native_allocator,
        {"allocate", "deallocate"},
        "reference native allocator",
    )
    parse_rva(native_allocator.get("allocate"), "native allocator allocate")
    parse_rva(native_allocator.get("deallocate"), "native allocator deallocate")
    init_count = require_positive_integer(
        profile.get("init_array_count"), "init-array count"
    )
    parse_rva(profile.get("init_array_offset"), "init-array offset")
    validated_ranges(
        profile.get("constructor_run_ranges"), "constructor ranges", init_count
    )
    validated_ranges(
        profile.get("native_mimalloc_constructor_run_ranges"),
        "native mimalloc constructor ranges",
        init_count,
    )
    thread_index = profile.get("native_mimalloc_thread_initializer_after_constructor")
    if (
        not isinstance(thread_index, int)
        or isinstance(thread_index, bool)
        or not 0 <= thread_index < init_count
    ):
        raise AnalyzerError("reference native mimalloc thread boundary is invalid")
    bootstrap = profile.get("native_pre_jni_bootstrap")
    if not isinstance(bootstrap, dict):
        raise AnalyzerError("reference native pre-JNI bootstrap is unavailable")
    require_exact_keys(
        bootstrap,
        {"registry_initializer", "registry_slot"},
        "reference native pre-JNI bootstrap",
    )
    parse_rva(bootstrap.get("registry_initializer"), "registry initializer")
    parse_rva(bootstrap.get("registry_slot"), "registry slot")
    if profile.get("default_allocator_strategy") != "native_mimalloc":
        raise AnalyzerError("reference profile must retain native mimalloc")

    for name in (
        "allocator_object_initializer_rva",
        "empty_string_initializer_rva",
        "jni_singleton_initializer_rva",
    ):
        parse_rva(anchors.get(name), f"derivation anchor {name}")
    constructor_rvas = anchors.get("constructor_rvas")
    if not isinstance(constructor_rvas, dict) or set(constructor_rvas) != {
        "2",
        "3",
        "4",
        "5",
    }:
        raise AnalyzerError("reference constructor derivation anchors are incomplete")
    for index, value in constructor_rvas.items():
        parse_rva(value, f"constructor {index} anchor")


def validated_payload_metadata(
    path: Path, candidate_path: Path, candidate: ElfImage
) -> dict[str, Any]:
    metadata = read_json_object(path, "payload metadata")
    if path.parent.resolve() != candidate_path.parent.resolve():
        raise AnalyzerError("payload metadata and candidate ELF must share a directory")
    version_name = metadata.get("version_name")
    version_code = metadata.get("version_code")
    build_id = metadata.get("elf_build_id")
    sha = metadata.get("sha256")
    expected_sha = sha.get("libroblox") if isinstance(sha, dict) else None
    if (
        metadata.get("schema_version") != 1
        or metadata.get("package") != "com.roblox.client"
        or metadata.get("abi") != "x86_64"
        or not isinstance(version_name, str)
        or not version_name
        or not isinstance(version_code, int)
        or isinstance(version_code, bool)
        or version_code <= 0
        or not isinstance(build_id, str)
        or ELF_BUILD_ID_PATTERN.fullmatch(build_id) is None
        or not isinstance(expected_sha, str)
        or SHA256_PATTERN.fullmatch(expected_sha) is None
    ):
        raise AnalyzerError("payload metadata identity is incomplete or invalid")
    if build_id != candidate.build_id or expected_sha != candidate.sha256:
        raise AnalyzerError("payload metadata does not match candidate ELF bytes")
    payload_id = f"{version_code}-{build_id}"
    if candidate_path.parent.name != payload_id:
        raise AnalyzerError("candidate ELF parent is not the canonical payload ID")
    return metadata


def disassemble(image: ElfImage, rva: int, instruction_count: int) -> tuple[Any, ...]:
    require_capstone()
    if instruction_count <= 0 or instruction_count > 256:
        raise AnalyzerError("signature instruction count is invalid")
    image.require_code_rva(rva)
    offset = image.rva_to_offset(rva)
    available = min(4096, image.file_size - offset)
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    decoder.detail = True
    instructions = tuple(
        decoder.disasm(image.bytes_at(offset, available, "code signature"), rva)
    )[:instruction_count]
    if len(instructions) != instruction_count or instructions[0].address != rva:
        raise AnalyzerError(
            f"cannot decode {instruction_count} instructions at 0x{rva:x}"
        )
    expected_address = rva
    for instruction in instructions:
        if instruction.address != expected_address or instruction.size <= 0:
            raise AnalyzerError(f"non-contiguous instruction signature at 0x{rva:x}")
        expected_address += instruction.size
    image.require_code_rva(rva, expected_address - rva)
    return instructions


def signature_bytes(
    image: ElfImage, rva: int, instructions: Sequence[Any]
) -> tuple[bytes, bytes]:
    end = instructions[-1].address + instructions[-1].size
    offset = image.rva_to_offset(rva, end - rva)
    encoded = bytearray(image.bytes_at(offset, end - rva, "code signature"))
    mask = bytearray(b"\x01" * len(encoded))
    for instruction in instructions:
        base = instruction.address - rva
        for operand_offset, operand_size in (
            (instruction.imm_offset, instruction.imm_size),
            (instruction.disp_offset, instruction.disp_size),
        ):
            if operand_size == 0:
                continue
            if operand_offset <= 0 or operand_offset + operand_size > instruction.size:
                raise AnalyzerError(
                    "Capstone returned an invalid operand encoding range"
                )
            for index in range(operand_offset, operand_offset + operand_size):
                mask[base + index] = 0
    return bytes(encoded), bytes(mask)


def longest_fixed_anchor(
    encoded: bytes, mask: bytes, minimum_bytes: int = 6
) -> tuple[int, bytes]:
    best_offset = -1
    best = b""
    start = None
    for index, enabled in enumerate(mask + b"\x00"):
        if enabled and start is None:
            start = index
        elif not enabled and start is not None:
            if index - start > len(best):
                best_offset = start
                best = encoded[start:index]
            start = None
    if best_offset < 0 or len(best) < minimum_bytes:
        raise AnalyzerError("normalized signature has no selective fixed anchor")
    return best_offset, best


def semantic_operand(
    instruction: Any, operand: Any, wildcard_size: bool
) -> tuple[Any, ...]:
    if operand.type == capstone_x86.X86_OP_REG:
        return ("reg", instruction.reg_name(operand.reg), operand.size)
    if operand.type == capstone_x86.X86_OP_IMM:
        is_control_flow = instruction.group(capstone.CS_GRP_CALL) or instruction.group(
            capstone.CS_GRP_JUMP
        )
        if is_control_flow:
            value: Any = "control-flow-target"
        elif wildcard_size:
            value = "registry-object-size"
        else:
            value = operand.imm
        return ("imm", value, operand.size)
    if operand.type == capstone_x86.X86_OP_MEM:
        memory = operand.mem
        base = instruction.reg_name(memory.base) if memory.base else ""
        index = instruction.reg_name(memory.index) if memory.index else ""
        displacement: Any = (
            "rip-target" if memory.base == capstone_x86.X86_REG_RIP else memory.disp
        )
        return (
            "mem",
            instruction.reg_name(memory.segment) if memory.segment else "",
            base,
            index,
            memory.scale,
            displacement,
            operand.size,
        )
    raise AnalyzerError("normalized signature contains an unsupported operand")


def semantic_shape(
    instruction: Any, wildcard_registry_object_size: bool = False
) -> tuple[Any, ...]:
    return (
        instruction.mnemonic,
        tuple(
            semantic_operand(instruction, operand, wildcard_registry_object_size)
            for operand in instruction.operands
        ),
    )


def validate_semantic_match(
    reference: Sequence[Any],
    candidate: Sequence[Any],
    spec: SignatureSpec,
) -> None:
    if len(reference) != len(candidate):
        raise AnalyzerError("normalized signature instruction count changed")
    for index, (reference_instruction, candidate_instruction) in enumerate(
        zip(reference, candidate)
    ):
        wildcard_size = (
            spec.allow_registry_object_size_change
            and index in spec.registry_size_indices
        )
        if semantic_shape(reference_instruction, wildcard_size) != semantic_shape(
            candidate_instruction, wildcard_size
        ):
            raise AnalyzerError(
                f"normalized instruction semantics changed at signature index {index}"
            )
    if spec.allow_registry_object_size_change:
        sizes = []
        for index in spec.registry_size_indices:
            if index >= len(candidate):
                raise AnalyzerError(
                    "registry initializer size index is outside its signature"
                )
            operands = candidate[index].operands
            if len(operands) != 2 or operands[1].type != capstone_x86.X86_OP_IMM:
                raise AnalyzerError("registry initializer size operands changed")
            sizes.append(operands[1].imm)
        if (
            sizes[0] != sizes[1]
            or sizes[0] < 0x100
            or sizes[0] > 0x1000
            or sizes[0] % 8
        ):
            raise AnalyzerError("registry initializer object size is inconsistent")


def find_signature_matches(
    reference: ElfImage,
    candidate: ElfImage,
    spec: SignatureSpec,
    *,
    allow_multiple_candidates: bool = False,
) -> tuple[SignatureMatch, ...]:
    reference_instructions = disassemble(
        reference, spec.reference_rva, spec.instruction_count
    )
    encoded, mask = signature_bytes(
        reference, spec.reference_rva, reference_instructions
    )
    anchor_offset, anchor = longest_fixed_anchor(
        encoded, mask, spec.minimum_anchor_bytes
    )
    text = candidate.sections[".text"]
    text_begin = text.offset
    text_end = text.offset + text.size
    matches = []
    search_offset = text_begin
    while True:
        anchor_position = candidate.data.find(anchor, search_offset, text_end)
        if anchor_position < 0:
            break
        signature_offset = anchor_position - anchor_offset
        if (
            text_begin <= signature_offset
            and signature_offset + len(encoded) <= text_end
        ):
            candidate_bytes = candidate.data[
                signature_offset : signature_offset + len(encoded)
            ]
            if all(
                not mask[index] or candidate_bytes[index] == encoded[index]
                for index in range(len(encoded))
            ):
                segment = candidate.segment_for_rva(
                    text.address + signature_offset - text.offset, len(encoded)
                )
                if (
                    segment is not None
                    and segment.flags & PF_EXECUTE
                    and not segment.flags & PF_WRITE
                ):
                    matches.append(signature_offset)
        search_offset = anchor_position + 1
    if not matches:
        raise AnalyzerError(
            f"signature {spec.name} matched 0 candidate locations"
        )
    if not allow_multiple_candidates and len(matches) != 1:
        raise AnalyzerError(
            f"signature {spec.name} matched {len(matches)} candidate locations"
        )
    semantic_matches = []
    semantic_errors = []
    for match in matches:
        match_rva = text.address + match - text.offset
        candidate_instructions = disassemble(
            candidate, match_rva, spec.instruction_count
        )
        try:
            validate_semantic_match(
                reference_instructions,
                candidate_instructions,
                spec,
            )
        except AnalyzerError as error:
            semantic_errors.append(error)
            continue
        semantic_matches.append(
            SignatureMatch(
                match_rva, reference_instructions, candidate_instructions
            )
        )
    if not semantic_matches and len(matches) == 1 and semantic_errors:
        raise semantic_errors[0]
    return tuple(semantic_matches)


def find_signature_match(
    reference: ElfImage, candidate: ElfImage, spec: SignatureSpec
) -> SignatureMatch:
    matches = find_signature_matches(reference, candidate, spec)
    if len(matches) != 1:
        raise AnalyzerError(
            f"signature {spec.name} matched {len(matches)} candidate locations"
        )
    return matches[0]


def unique_semantic_signature_match(
    reference: ElfImage, candidate: ElfImage, spec: SignatureSpec
) -> SignatureMatch:
    matches = find_signature_matches(
        reference, candidate, spec, allow_multiple_candidates=True
    )
    if len(matches) != 1:
        raise AnalyzerError(
            f"signature {spec.name} matched {len(matches)} semantic candidate locations"
        )
    return matches[0]


def relative_relocation_map(image: ElfImage) -> dict[int, int]:
    result = {}
    for relocation in decode_aps2_relocations(
        image.section_bytes(image.sections[".rela.dyn"])
    ):
        if (
            relocation.info & 0xFFFFFFFF != R_X86_64_RELATIVE
            or relocation.info >> 32 != 0
        ):
            continue
        if relocation.offset in result:
            raise AnalyzerError("ELF contains duplicate relative relocations")
        result[relocation.offset] = relocation.addend
    return result


def has_fullscreen_setter_contract(image: ElfImage, rva: int) -> bool:
    size = 96
    try:
        image.require_code_rva(rva, size)
        offset = image.rva_to_offset(rva, size)
        code = image.bytes_at(offset, size, "fullscreen setter contract")
    except AnalyzerError:
        return False
    fields = (b"\x59\x01\x00\x00", b"\x61\x01\x00\x00")
    return (
        code.startswith(b"\x55\x48\x89\xe5")
        and b"\x89\xf3" in code[:24]
        and any(
            b"\x38\x98" + field in code and b"\x88\x98" + field in code
            for field in fields
        )
    )


def has_fmod_string_constructor_contract(image: ElfImage, rva: int) -> bool:
    expected = bytes.fromhex(
        "55 48 89 e5 41 57 41 56 41 54 53 48 89 d3 49 89 "
        "f6 49 89 ff 48 83 fa 16"
    )
    try:
        image.require_code_rva(rva, len(expected))
        offset = image.rva_to_offset(rva, len(expected))
        return (
            image.bytes_at(offset, len(expected), "FMOD string contract")
            == expected
        )
    except AnalyzerError:
        return False


def has_fmod_device_lists_select_contract(image: ElfImage, rva: int) -> bool:
    # Keep in sync with compat/fmod_output_device_contract.h. The fixed bytes
    # bind the int-index ABI, count slot, clamp, and FMOD system-object offset.
    expected = bytes.fromhex(
        "55 48 89 e5 41 57 41 56 41 54 53 48 83 ec 50 41 "
        "89 f7 49 89 fe 4c 8b 25 00 00 00 00 49 8b 04 24 "
        "48 89 45 d8 80 3d 00 00 00 00 00 74 25 49 8b 04 "
        "24 48 3b 45 d8 0f 85 00 00 00 00 4c 89 f7 44 89 "
        "fe 48 83 c4 50 5b 41 5c 41 5e 41 5f 5d e9 00 00 "
        "00 00 49 8b 06 4c 89 f7 ff 50 28 44 39 f8 0f 8e "
        "00 00 00 00 31 db 45 85 ff 41 0f 4f df 49 8b 7e "
        "28 89 de e8 00 00 00 00"
    )
    try:
        image.require_code_rva(rva, len(expected))
        offset = image.rva_to_offset(rva, len(expected))
        code = image.bytes_at(offset, len(expected), "FMOD device-list selector")
    except AnalyzerError:
        return False
    relocated = {index for start in (24, 38, 55, 78, 96, 116)
                 for index in range(start, start + 4)}
    return len(code) == len(expected) and all(
        index in relocated or actual == wanted
        for index, (actual, wanted) in enumerate(zip(code, expected))
    )


def load_reference_runtime_profile(
    paths: Sequence[Path], reference_build_id: str
) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for path in paths:
        document = read_json_object(path, "reference compatibility manifest")
        profiles = document.get("profiles")
        if document.get("schema_version") != 1 or not isinstance(profiles, list):
            raise AnalyzerError(
                "reference compatibility manifest schema is unsupported"
            )
        for profile in profiles:
            if (
                not isinstance(profile, dict)
                or profile.get("elf_build_id") != reference_build_id
            ):
                continue
            discovered: dict[str, Any] = {}
            if "user_game_settings_fullscreen_setter_rva" in profile:
                discovered["user_game_settings_fullscreen_setter_rva"] = parse_rva(
                    profile["user_game_settings_fullscreen_setter_rva"],
                    "reference fullscreen setter",
                )
            if "fmod_output_device_bridge" in profile:
                bridge = profile["fmod_output_device_bridge"]
                fields = {
                    "vtable_rva",
                    "string_constructor_rva",
                    "count_method_rva",
                    "info_method_rva",
                    "current_method_rva",
                    "select_method_rva",
                }
                if (
                    not isinstance(bridge, dict)
                    or not fields <= set(bridge)
                    or set(bridge) - fields - {"vtable_layout_version"} - {
                        "input_count_method_rva", "input_info_method_rva",
                        "input_current_method_rva", "input_select_method_rva",
                    }
                    or profile.get("allow_host_abi_bridges") is not True
                ):
                    raise AnalyzerError(
                        "reference FMOD output-device profile is incomplete"
                    )
                discovered["fmod_output_device_bridge"] = {
                    field: parse_rva(value, f"reference FMOD {field}")
                    for field, value in bridge.items()
                    if field != "vtable_layout_version"
                }
                layout = bridge.get("vtable_layout_version", 1)
                if type(layout) is not int or layout not in (1, 2):
                    raise AnalyzerError("reference FMOD vtable layout is unsupported")
                if layout != 1:
                    discovered["fmod_output_device_bridge"]["vtable_layout_version"] = layout
            for field, value in discovered.items():
                if field in result:
                    previous = result[field]
                    if field == "fmod_output_device_bridge":
                        input_fields = {
                            "input_count_method_rva", "input_info_method_rva",
                            "input_current_method_rva", "input_select_method_rva",
                        }
                        compatible = ((set(previous) ^ set(value)) <= input_fields and
                            all(previous[key] == value[key]
                                for key in previous.keys() & value.keys()))
                        if compatible:
                            value = {**previous, **value}
                    else:
                        compatible = previous == value
                    if not compatible:
                        raise AnalyzerError(
                            f"reference compatibility manifests disagree on {field}"
                        )
                result[field] = value
    return result


def derive_fmod_output_device_bridge(
    reference: ElfImage,
    candidate: ElfImage,
    source: dict[str, int],
) -> dict[str, Any]:
    source_layout = source.get("vtable_layout_version", 1)
    indexes = {
        "count_method_rva": 5,
        "info_method_rva": 6,
        "current_method_rva": 8 if source_layout == 2 else 7,
        "select_method_rva": 19 if source_layout == 2 else 17,
    }
    source_vtable = source["vtable_rva"]
    reference.require_relro_rva(source_vtable, (max(indexes.values()) + 1) * 8)
    reference_relocations = relative_relocation_map(reference)
    for field, index in indexes.items():
        if reference_relocations.get(source_vtable + index * 8) != source[field]:
            raise AnalyzerError("reference FMOD vtable does not match its methods")
        reference.require_code_rva(source[field])
    if not has_fmod_string_constructor_contract(
        reference, source["string_constructor_rva"]
    ):
        raise AnalyzerError("reference FMOD string constructor contract changed")

    string_constructor = unique_semantic_signature_match(
        reference,
        candidate,
        SignatureSpec(
            "fmod-string-constructor",
            source["string_constructor_rva"],
            32,
            minimum_anchor_bytes=3,
        ),
    ).rva
    info_method = unique_semantic_signature_match(
        reference,
        candidate,
        SignatureSpec(
            "fmod-output-info",
            source["info_method_rva"],
            18,
            minimum_anchor_bytes=3,
        ),
    ).rva
    try:
        select_method = unique_semantic_signature_match(
            reference,
            candidate,
            SignatureSpec(
                "fmod-output-select",
                source["select_method_rva"],
                18,
                minimum_anchor_bytes=3,
            ),
        ).rva
    except AnalyzerError:
        # The device-list layout has a separately checked selector contract.
        select_method = None
    count_candidates = {
        match.rva
        for match in find_signature_matches(
            reference,
            candidate,
            SignatureSpec(
                "fmod-output-count",
                source["count_method_rva"],
                24,
                minimum_anchor_bytes=3,
            ),
            allow_multiple_candidates=True,
        )
    }
    current_candidates = {
        match.rva
        for match in find_signature_matches(
            reference,
            candidate,
            SignatureSpec(
                "fmod-output-current",
                source["current_method_rva"],
                24,
                minimum_anchor_bytes=3,
            ),
            allow_multiple_candidates=True,
        )
    }
    if not count_candidates or not current_candidates:
        raise AnalyzerError("FMOD count/current method signatures have no candidates")

    relocations = relative_relocation_map(candidate)
    vtables = []
    for offset, addend in relocations.items():
        if addend != info_method or offset < indexes["info_method_rva"] * 8:
            continue
        vtable = offset - indexes["info_method_rva"] * 8
        count_method = relocations.get(vtable + indexes["count_method_rva"] * 8)
        for layout, current_index, select_index in ((1, 7, 17), (2, 8, 19)):
            current_method = relocations.get(vtable + current_index * 8)
            selected = relocations.get(vtable + select_index * 8)
            if (
                vtable % 8 != 0
                or count_method not in count_candidates
                or current_method not in current_candidates
                or count_method == current_method
                or selected is None
            ):
                continue
            try:
                candidate.require_relro_rva(vtable, (select_index + 1) * 8)
            except AnalyzerError:
                continue
            if layout == 1:
                if source_layout != 1 or select_method is None or selected != select_method:
                    continue
            elif not has_fmod_device_lists_select_contract(candidate, selected):
                continue
            vtables.append((vtable, count_method, current_method, selected, layout))
    if len(vtables) != 1:
        raise AnalyzerError(
            f"FMOD output vtable matched {len(vtables)} candidate locations"
        )
    vtable, count_method, current_method, select_method, layout = vtables[0]
    if not has_fmod_string_constructor_contract(candidate, string_constructor):
        raise AnalyzerError("candidate FMOD string constructor contract changed")
    result = {
        "vtable_rva": format_rva(vtable),
        "string_constructor_rva": format_rva(string_constructor),
        "count_method_rva": format_rva(count_method),
        "info_method_rva": format_rva(info_method),
        "current_method_rva": format_rva(current_method),
        "select_method_rva": format_rva(select_method),
    }
    if layout != 1:
        result["vtable_layout_version"] = layout
    input_fields = (
        "input_count_method_rva", "input_info_method_rva",
        "input_current_method_rva", "input_select_method_rva",
    )
    if any(field in source for field in input_fields):
        if not all(field in source for field in input_fields):
            raise AnalyzerError("reference FMOD input profile is incomplete")
        source_slots = (9, 10, 12, 18) if source_layout == 2 else (8, 9, 10, 16)
        target_slots = (9, 10, 12, 18) if layout == 2 else (8, 9, 10, 16)
        for field, old_slot, new_slot, length in zip(
            input_fields, source_slots, target_slots, (4, 16, 12, 16)
        ):
            if reference_relocations.get(source_vtable + old_slot * 8) != source[field]:
                raise AnalyzerError("reference FMOD input slot changed")
            target = relocations.get(vtable + new_slot * 8)
            matches = find_signature_matches(
                reference, candidate,
                SignatureSpec(field, source[field], length, minimum_anchor_bytes=3),
                allow_multiple_candidates=True,
            )
            if target is None or target not in {match.rva for match in matches}:
                raise AnalyzerError(f"candidate FMOD input contract changed: {field}")
            candidate.require_code_rva(target)
            result[field] = format_rva(target)
    return result


def derive_runtime_compatibility(
    reference: ElfImage,
    candidate: ElfImage,
    manifests: Sequence[Path],
) -> dict[str, Any]:
    source = load_reference_runtime_profile(manifests, reference.build_id)
    result: dict[str, Any] = {}
    fullscreen = source.get("user_game_settings_fullscreen_setter_rva")
    if fullscreen is not None:
        if not has_fullscreen_setter_contract(reference, fullscreen):
            raise AnalyzerError("reference fullscreen setter contract changed")
        setter = unique_semantic_signature_match(
            reference,
            candidate,
            SignatureSpec(
                "fullscreen-setter", fullscreen, 32, minimum_anchor_bytes=3
            ),
        ).rva
        if not has_fullscreen_setter_contract(candidate, setter):
            raise AnalyzerError("candidate fullscreen setter contract changed")
        result["user_game_settings_fullscreen_setter_rva"] = format_rva(setter)
    fmod = source.get("fmod_output_device_bridge")
    if fmod is not None:
        result["fmod_output_device_bridge"] = derive_fmod_output_device_bridge(
            reference, candidate, fmod
        )
    return result


def find_registry_function_entry(
    candidate: ElfImage,
    body_rva: int,
    reference_prologue: Sequence[Any],
) -> int:
    prologue_instruction_count = 8
    maximum_prologue_bytes = 64
    if (
        len(reference_prologue) != prologue_instruction_count
        or body_rva < maximum_prologue_bytes
    ):
        raise AnalyzerError("reference registry prologue shape is invalid")
    entries = []
    for rva in range(body_rva - maximum_prologue_bytes, body_rva):
        try:
            instructions = disassemble(candidate, rva, prologue_instruction_count)
        except AnalyzerError:
            continue
        if instructions[-1].address + instructions[-1].size != body_rva:
            continue
        if any(
            semantic_shape(reference_instruction, index == 7)
            != semantic_shape(candidate_instruction, index == 7)
            for index, (reference_instruction, candidate_instruction) in enumerate(
                zip(reference_prologue, instructions)
            )
        ):
            continue
        frame = instructions[-1]
        operands = frame.operands
        if (
            frame.mnemonic == "sub"
            and len(operands) == 2
            and operands[0].type == capstone_x86.X86_OP_REG
            and operands[0].reg == capstone_x86.X86_REG_RSP
            and operands[1].type == capstone_x86.X86_OP_IMM
            and 0x20 <= operands[1].imm <= 0x400
            and operands[1].imm % 8 == 0
        ):
            entries.append(rva)
    if len(entries) != 1:
        raise AnalyzerError(
            "registry initializer body has no unique bounded prologue"
        )
    return entries[0]


def registry_rip_target(instruction: Any, operand: Any) -> int | None:
    if (
        operand.type != capstone_x86.X86_OP_MEM
        or operand.mem.base != capstone_x86.X86_REG_RIP
        or operand.mem.index != 0
        or operand.size != 8
    ):
        return None
    return instruction.address + instruction.size + operand.mem.disp


def registry_registration_link(
    image: ElfImage, instructions: Sequence[Any]
) -> tuple[int, int] | None:
    if len(instructions) != 6:
        return None
    first, store_function, load_function_slot, load_auxiliary, store_auxiliary, restore_auxiliary = instructions

    def is_register(instruction: Any, operand: Any, name: str) -> bool:
        return (
            operand.type == capstone_x86.X86_OP_REG
            and instruction.reg_name(operand.reg) == name
            and operand.size == 8
        )

    if (
        first.mnemonic != "lea"
        or len(first.operands) != 2
        or not is_register(first, first.operands[0], "rcx")
        or registry_rip_target(first, first.operands[1]) is None
        or store_function.mnemonic != "mov"
        or len(store_function.operands) != 2
        or registry_rip_target(store_function, store_function.operands[0]) is None
        or not is_register(store_function, store_function.operands[1], "rcx")
        or load_function_slot.mnemonic != "lea"
        or len(load_function_slot.operands) != 2
        or not is_register(load_function_slot, load_function_slot.operands[0], "rcx")
        or registry_rip_target(load_function_slot, load_function_slot.operands[1]) is None
        or load_auxiliary.mnemonic != "mov"
        or len(load_auxiliary.operands) != 2
        or not is_register(load_auxiliary, load_auxiliary.operands[0], "rdx")
        or registry_rip_target(load_auxiliary, load_auxiliary.operands[1]) is None
        or store_auxiliary.mnemonic != "mov"
        or len(store_auxiliary.operands) != 2
        or registry_rip_target(store_auxiliary, store_auxiliary.operands[0]) is None
        or not is_register(store_auxiliary, store_auxiliary.operands[1], "rdx")
        or restore_auxiliary.mnemonic != "mov"
        or len(restore_auxiliary.operands) != 2
        or registry_rip_target(restore_auxiliary, restore_auxiliary.operands[0]) is None
        or not is_register(restore_auxiliary, restore_auxiliary.operands[1], "rcx")
    ):
        return None
    initializer = registry_rip_target(first, first.operands[1])
    pointer_slot = registry_rip_target(store_function, store_function.operands[0])
    linked_pointer_slot = registry_rip_target(
        load_function_slot, load_function_slot.operands[1]
    )
    auxiliary_slot = registry_rip_target(load_auxiliary, load_auxiliary.operands[1])
    stored_auxiliary_slot = registry_rip_target(
        store_auxiliary, store_auxiliary.operands[0]
    )
    restored_auxiliary_slot = registry_rip_target(
        restore_auxiliary, restore_auxiliary.operands[0]
    )
    if (
        initializer is None
        or pointer_slot is None
        or linked_pointer_slot != pointer_slot
        or auxiliary_slot is None
        or stored_auxiliary_slot != pointer_slot + 8
        or restored_auxiliary_slot != auxiliary_slot
    ):
        return None
    try:
        image.require_code_rva(initializer)
        image.require_writable_rva(pointer_slot, 8)
        image.require_writable_rva(pointer_slot + 8, 8)
    except AnalyzerError:
        return None
    return initializer, pointer_slot


def find_registry_registration_links(image: ElfImage) -> tuple[tuple[int, int], ...]:
    text = image.sections.get(".text")
    if text is None:
        raise AnalyzerError("ELF has no code section for registry registrations")
    anchor = b"\x48\x8d\x0d"
    links = []
    search_offset = text.offset
    text_end = text.offset + text.size
    while True:
        anchor_position = image.data.find(anchor, search_offset, text_end)
        if anchor_position < 0:
            break
        rva = text.address + anchor_position - text.offset
        try:
            instructions = disassemble(image, rva, 6)
        except AnalyzerError:
            instructions = ()
        link = registry_registration_link(image, instructions)
        if link is not None:
            links.append(link)
        search_offset = anchor_position + 1
    return tuple(links)


def saved_32_bit_register(prologue: Sequence[Any], register_name: str) -> bool:
    if len(register_name) == 3 and register_name.startswith("e"):
        full_name = "r" + register_name[1:]
    elif len(register_name) >= 4 and register_name.startswith("r") and register_name.endswith("d"):
        full_name = register_name[:-1]
    else:
        return False
    return any(
        instruction.mnemonic == "push"
        and len(instruction.operands) == 1
        and instruction.operands[0].type == capstone_x86.X86_OP_REG
        and instruction.reg_name(instruction.operands[0].reg) == full_name
        and instruction.operands[0].size == 8
        for instruction in prologue
    )


def self_xor_register(instruction: Any) -> str | None:
    if (
        instruction.mnemonic != "xor"
        or len(instruction.operands) != 2
        or any(operand.type != capstone_x86.X86_OP_REG for operand in instruction.operands)
        or any(operand.size != 4 for operand in instruction.operands)
        or instruction.operands[0].reg != instruction.operands[1].reg
    ):
        return None
    return instruction.reg_name(instruction.operands[0].reg)


def registry_field_memory(
    instruction: Any, operand: Any, base: str, displacement: int, size: int
) -> bool:
    return (
        operand.type == capstone_x86.X86_OP_MEM
        and operand.mem.base != capstone_x86.X86_REG_RIP
        and instruction.reg_name(operand.mem.base) == base
        and operand.mem.index == 0
        and operand.mem.disp == displacement
        and operand.size == size
    )


def validate_registry_initializer_prefix(
    reference: Sequence[Any], candidate: Sequence[Any]
) -> bool:
    body = 8
    if len(reference) < body + 11 or len(candidate) < body + 11:
        return False
    if not bounded_registry_stack_frame(reference[7]) or not bounded_registry_stack_frame(
        candidate[7]
    ):
        return False
    if any(
        semantic_shape(reference[index], index == 7)
        != semantic_shape(candidate[index], index == 7)
        for index in range(body)
    ):
        return False

    def move_immediate(instruction: Any, destination: str) -> int | None:
        if (
            instruction.mnemonic != "mov"
            or len(instruction.operands) != 2
            or instruction.operands[0].type != capstone_x86.X86_OP_REG
            or instruction.reg_name(instruction.operands[0].reg) != destination
            or instruction.operands[0].size != 4
            or instruction.operands[1].type != capstone_x86.X86_OP_IMM
            or instruction.operands[1].size != 4
        ):
            return None
        return instruction.operands[1].imm

    old_alloc_size = move_immediate(reference[body], "edi")
    new_alloc_size = move_immediate(candidate[body], "edi")
    old_zero_size = move_immediate(reference[body + 4], "edx")
    new_zero_size = move_immediate(candidate[body + 4], "edx")
    if (
        old_alloc_size is None
        or new_alloc_size is None
        or old_zero_size != old_alloc_size
        or new_zero_size != new_alloc_size
        or not 0x100 <= old_alloc_size <= 0x1000
        or not 0x100 <= new_alloc_size <= 0x1000
        or old_alloc_size % 8
        or new_alloc_size % 8
    ):
        return False
    if any(
        semantic_shape(reference[index]) != semantic_shape(candidate[index])
        for index in (body + 1, body + 2, body + 5, body + 6, body + 7)
    ):
        return False
    old_zero_register = self_xor_register(reference[body + 3])
    new_zero_register = self_xor_register(candidate[body + 3])
    if (
        old_zero_register is None
        or new_zero_register is None
        or not saved_32_bit_register(reference[:body], old_zero_register)
        or not saved_32_bit_register(candidate[:body], new_zero_register)
    ):
        return False

    old_constant = reference[body + 8]
    new_constant = candidate[body + 8]
    if any(
        instruction.mnemonic != "mov"
        or len(instruction.operands) != 2
        or instruction.operands[0].type != capstone_x86.X86_OP_REG
        or instruction.operands[0].size != 4
        or instruction.operands[1].type != capstone_x86.X86_OP_IMM
        or instruction.operands[1].imm != 0x3F800000
        for instruction in (old_constant, new_constant)
    ):
        return False
    old_field = reference[body + 9]
    new_field = candidate[body + 9]
    if (
        old_field.mnemonic != "mov"
        or len(old_field.operands) != 2
        or new_field.mnemonic != "mov"
        or len(new_field.operands) != 2
        or not registry_field_memory(old_field, old_field.operands[0], "rbx", 0x20, 4)
        or not registry_field_memory(new_field, new_field.operands[0], "rbx", 0x20, 4)
        or old_field.operands[1].type != capstone_x86.X86_OP_REG
        or new_field.operands[1].type != capstone_x86.X86_OP_REG
        or old_field.operands[1].reg != old_constant.operands[0].reg
        or new_field.operands[1].reg != new_constant.operands[0].reg
    ):
        return False
    for instruction in (reference[body + 10], candidate[body + 10]):
        if (
            instruction.mnemonic != "lea"
            or len(instruction.operands) != 2
            or instruction.operands[0].type != capstone_x86.X86_OP_REG
            or instruction.operands[0].size != 8
            or not registry_field_memory(instruction, instruction.operands[1], "rbx", 0x28, 8)
        ):
            return False
    return True


def bounded_registry_stack_frame(instruction: Any) -> bool:
    if instruction.mnemonic != "sub" or len(instruction.operands) != 2:
        return False
    destination, amount = instruction.operands
    return (
        destination.type == capstone_x86.X86_OP_REG
        and destination.reg == capstone_x86.X86_REG_RSP
        and amount.type == capstone_x86.X86_OP_IMM
        and 0x20 <= amount.imm <= 0x400
        and amount.imm % 8 == 0
    )


def find_registry_registration_semantic_match(
    reference: ElfImage,
    candidate: ElfImage,
    reference_full: Sequence[Any],
    reference_slot: int,
    allocate_matches: Sequence[SignatureMatch],
) -> SignatureMatch:
    if not reference_full:
        raise AnalyzerError("reference registry initializer is empty")
    if derive_registry_slot(reference, reference_full[0].address) != reference_slot:
        raise AnalyzerError(
            "reference registry initializer does not publish its sidecar slot"
        )
    reference_links = tuple(
        link
        for link in find_registry_registration_links(reference)
        if link[0] == reference_full[0].address
    )
    if (
        len(reference_links) != 1
        or reference_links[0][1] + 16 != reference_slot
    ):
        raise AnalyzerError(
            "reference registry initializer has no unique linked singleton slot"
        )
    reference_memset_stub = find_imported_plt_stub(reference, "memset")
    if len(reference_full) <= 15 or direct_call_target(
        reference_full[15], "reference registry zeroing call"
    ) != reference_memset_stub:
        raise AnalyzerError(
            "reference registry zeroing call is not the verified memset import"
        )
    candidate_memset_stub = find_imported_plt_stub(candidate, "memset")
    semantic_matches = []
    for initializer, pointer_slot in find_registry_registration_links(candidate):
        try:
            candidate_full = disassemble(candidate, initializer, len(reference_full))
            if not validate_registry_initializer_prefix(reference_full, candidate_full):
                continue
            if direct_call_target(
                candidate_full[15], "candidate registry zeroing call"
            ) != candidate_memset_stub:
                continue
            allocator = direct_call_target(
                candidate_full[9], "candidate registry allocator call"
            )
            if sum(match.rva == allocator for match in allocate_matches) != 1:
                continue
            registry_slot = derive_registry_slot(candidate, initializer)
            if pointer_slot + 16 != registry_slot:
                continue
            candidate.require_writable_rva(registry_slot, 8)
        except AnalyzerError:
            continue
        semantic_matches.append(
            SignatureMatch(initializer, tuple(reference_full), candidate_full)
        )
    if len(semantic_matches) != 1:
        raise AnalyzerError(
            "registered registry initializer matched "
            f"{len(semantic_matches)} semantic candidate locations"
        )
    return semantic_matches[0]


def find_registry_signature_match(
    reference: ElfImage,
    candidate: ElfImage,
    spec: SignatureSpec,
    reference_slot: int,
    allocate_matches: Sequence[SignatureMatch],
) -> SignatureMatch:
    body_instruction_index = 8
    body_instruction_count = 11
    reference_full = disassemble(
        reference, spec.reference_rva, spec.instruction_count
    )
    if len(reference_full) < body_instruction_index + body_instruction_count:
        raise AnalyzerError("reference registry signature is incomplete")
    body_spec = SignatureSpec(
        "registry-initializer-body",
        reference_full[body_instruction_index].address,
        body_instruction_count,
        True,
        spec.minimum_anchor_bytes,
        (0, 4),
    )
    try:
        body_match = find_signature_match(reference, candidate, body_spec)
        candidate_entry = find_registry_function_entry(
            candidate,
            body_match.rva,
            reference_full[:body_instruction_index],
        )
        candidate_full = disassemble(
            candidate, candidate_entry, spec.instruction_count
        )
        if candidate_full[body_instruction_index].address != body_match.rva:
            raise AnalyzerError("candidate registry signature is incomplete")
        return SignatureMatch(candidate_entry, reference_full, candidate_full)
    except AnalyzerError as exact_error:
        try:
            return find_registry_registration_semantic_match(
                reference,
                candidate,
                reference_full,
                reference_slot,
                allocate_matches,
            )
        except AnalyzerError as semantic_error:
            raise semantic_error from exact_error


def direct_call_target(instruction: Any, description: str) -> int:
    if (
        instruction.mnemonic != "call"
        or len(instruction.operands) != 1
        or instruction.operands[0].type != capstone_x86.X86_OP_IMM
    ):
        raise AnalyzerError(f"{description} is not one direct call")
    return instruction.operands[0].imm


def direct_jump_target(instruction: Any, description: str) -> int:
    if (
        instruction.mnemonic != "jmp"
        or len(instruction.operands) != 1
        or instruction.operands[0].type != capstone_x86.X86_OP_IMM
    ):
        raise AnalyzerError(f"{description} is not one direct jump")
    return instruction.operands[0].imm


def find_imported_plt_stub(image: ElfImage, symbol_name: str) -> int:
    imported_slot, relocation_index = image.jump_slot_for_imported_function(
        symbol_name
    )
    plt = image.sections.get(".plt")
    if (
        plt is None
        or plt.section_type != SHT_PROGBITS
        or plt.flags & (SHF_ALLOC | SHF_EXECINSTR)
        != (SHF_ALLOC | SHF_EXECINSTR)
        or plt.flags & SHF_WRITE
        or plt.size <= 16
        or plt.size % 16
    ):
        raise AnalyzerError("ELF has no supported executable PLT")
    if plt.address > (1 << 64) - 1 - plt.size:
        raise AnalyzerError("ELF PLT address overflows")
    image.require_code_rva(plt.address, plt.size)
    matching_stubs = []
    for offset in range(16, plt.size, 16):
        try:
            stub = disassemble(image, plt.address + offset, 3)
        except AnalyzerError:
            continue
        first = stub[0]
        if (
            first.mnemonic != "jmp"
            or len(first.operands) != 1
            or registry_rip_target(first, first.operands[0]) is None
        ):
            continue
        if registry_rip_target(first, first.operands[0]) != imported_slot:
            continue
        push_index, jump_plt0 = stub[1:]
        if (
            push_index.mnemonic != "push"
            or len(push_index.operands) != 1
            or push_index.operands[0].type != capstone_x86.X86_OP_IMM
            or push_index.operands[0].imm != relocation_index
            or direct_jump_target(jump_plt0, "PLT entry resolver jump")
            != plt.address
            or sum(instruction.size for instruction in stub) != 16
        ):
            raise AnalyzerError(
                "PLT entry does not bind the requested import relocation"
            )
        matching_stubs.append(plt.address + offset)
    if len(matching_stubs) != 1:
        raise AnalyzerError(
            "ELF does not have one unique PLT stub for the requested import"
        )
    return matching_stubs[0]


def find_constructor_boundary_match(
    reference: ElfImage,
    candidate: ElfImage,
    spec: SignatureSpec,
    reference_entry: int,
    candidate_entry: int,
) -> SignatureMatch:
    if reference_entry != spec.reference_rva:
        raise AnalyzerError(
            "reference constructor boundary moved outside init-array index 4"
        )
    reference_wrapper = disassemble(reference, reference_entry, 1)
    reference_target = direct_jump_target(
        reference_wrapper[0], "reference constructor boundary"
    )
    target_match = find_signature_match(
        reference,
        candidate,
        SignatureSpec(
            "constructor-4-target",
            reference_target,
            spec.instruction_count,
        ),
    )
    candidate_wrapper = disassemble(candidate, candidate_entry, 1)
    candidate_target = direct_jump_target(
        candidate_wrapper[0], "candidate constructor boundary"
    )
    if candidate_target != target_match.rva:
        raise AnalyzerError(
            "constructor index 4 no longer targets its verified boundary"
        )
    return SignatureMatch(candidate_entry, reference_wrapper, candidate_wrapper)


def rip_targets(instruction: Any) -> tuple[int, ...]:
    targets = []
    for operand in instruction.operands:
        if (
            operand.type == capstone_x86.X86_OP_MEM
            and operand.mem.base == capstone_x86.X86_REG_RIP
        ):
            targets.append(instruction.address + instruction.size + operand.mem.disp)
    return tuple(targets)


def mapped_rip_target(
    match: SignatureMatch, reference_target: int, description: str
) -> int:
    candidate_targets = []
    for reference_instruction, candidate_instruction in zip(
        match.reference_instructions, match.candidate_instructions
    ):
        if reference_target in rip_targets(reference_instruction):
            targets = rip_targets(candidate_instruction)
            if len(targets) != len(rip_targets(reference_instruction)):
                raise AnalyzerError(f"{description} RIP-relative operand count changed")
            reference_positions = [
                index
                for index, target in enumerate(rip_targets(reference_instruction))
                if target == reference_target
            ]
            candidate_targets.extend(targets[index] for index in reference_positions)
    if not candidate_targets or len(set(candidate_targets)) != 1:
        raise AnalyzerError(f"{description} has no unique mapped RIP target")
    return candidate_targets[0]


def exact_rip_target(instruction: Any, operand: Any, size: int) -> int | None:
    if (
        operand.type != capstone_x86.X86_OP_MEM
        or operand.size != size
        or operand.mem.base != capstone_x86.X86_REG_RIP
        or operand.mem.index != 0
        or operand.mem.segment != 0
    ):
        return None
    target = instruction.address + instruction.size + operand.mem.disp
    return target if 0 <= target <= UINT64_MAX else None


def is_register(operand: Any, register: int, size: int) -> bool:
    return (
        operand.type == capstone_x86.X86_OP_REG
        and operand.reg == register
        and operand.size == size
    )


def is_jni_immediate_store(
    instruction: Any, memory_size: int, target: int, value: int
) -> bool:
    return (
        instruction.mnemonic == "mov"
        and len(instruction.operands) == 2
        and exact_rip_target(instruction, instruction.operands[0], memory_size)
        == target
        and instruction.operands[1].type == capstone_x86.X86_OP_IMM
        and instruction.operands[1].imm == value
    )


def match_legacy_jni_singleton_layout(
    image: ElfImage, instructions: Sequence[Any]
) -> int | None:
    if len(instructions) != 11:
        return None
    lea, exchange, publish = instructions[:3]
    lea_target = (
        exact_rip_target(lea, lea.operands[1], 8)
        if lea.mnemonic == "lea"
        and len(lea.operands) == 2
        and is_register(lea.operands[0], capstone_x86.X86_REG_RAX, 8)
        else None
    )
    slot = (
        exact_rip_target(exchange, exchange.operands[0], 8)
        if exchange.mnemonic == "xchg"
        and len(exchange.operands) == 2
        and is_register(exchange.operands[1], capstone_x86.X86_REG_RAX, 8)
        else None
    )
    published_target = (
        exact_rip_target(publish, publish.operands[0], 8)
        if publish.mnemonic == "mov"
        and len(publish.operands) == 2
        and is_register(publish.operands[1], capstone_x86.X86_REG_RAX, 8)
        else None
    )
    if slot is None or slot % 8 or lea_target != slot + 8 or published_target != slot + 8:
        return None
    clear = instructions[3]
    if not (
        clear.mnemonic == "xorps"
        and len(clear.operands) == 2
        and is_register(clear.operands[0], capstone_x86.X86_REG_XMM0, 16)
        and is_register(clear.operands[1], capstone_x86.X86_REG_XMM0, 16)
    ):
        return None
    for index, offset in enumerate((0x10, 0x20, 0x30, 0x40), start=4):
        zero = instructions[index]
        if not (
            zero.mnemonic == "movups"
            and len(zero.operands) == 2
            and exact_rip_target(zero, zero.operands[0], 16) == slot + offset
            and is_register(zero.operands[1], capstone_x86.X86_REG_XMM0, 16)
        ):
            return None
    if not (
        is_jni_immediate_store(instructions[8], 8, slot + 0x50, 0)
        and is_jni_immediate_store(instructions[9], 4, slot + 0x58, 0x3F800000)
        and instructions[10].mnemonic == "ret"
        and not instructions[10].operands
    ):
        return None
    try:
        image.require_writable_rva(slot, 0x60)
    except AnalyzerError:
        return None
    return slot


def match_helper_jni_singleton_layout(
    image: ElfImage, instructions: Sequence[Any]
) -> int | None:
    if len(instructions) != 15:
        return None
    entry, frame, lea, exchange, publish, context, label, flags, initialize = (
        instructions[:9]
    )
    if not (
        entry.mnemonic == "push"
        and len(entry.operands) == 1
        and is_register(entry.operands[0], capstone_x86.X86_REG_RBP, 8)
        and frame.mnemonic == "mov"
        and len(frame.operands) == 2
        and is_register(frame.operands[0], capstone_x86.X86_REG_RBP, 8)
        and is_register(frame.operands[1], capstone_x86.X86_REG_RSP, 8)
        and lea.mnemonic == "lea"
        and len(lea.operands) == 2
        and is_register(lea.operands[0], capstone_x86.X86_REG_RAX, 8)
        and exchange.mnemonic == "xchg"
        and len(exchange.operands) == 2
        and is_register(exchange.operands[1], capstone_x86.X86_REG_RAX, 8)
        and publish.mnemonic == "mov"
        and len(publish.operands) == 2
        and is_register(publish.operands[1], capstone_x86.X86_REG_RAX, 8)
        and context.mnemonic == "lea"
        and len(context.operands) == 2
        and is_register(context.operands[0], capstone_x86.X86_REG_RDI, 8)
        and label.mnemonic == "lea"
        and len(label.operands) == 2
        and is_register(label.operands[0], capstone_x86.X86_REG_RSI, 8)
        and flags.mnemonic == "xor"
        and len(flags.operands) == 2
        and is_register(flags.operands[0], capstone_x86.X86_REG_EDX, 4)
        and is_register(flags.operands[1], capstone_x86.X86_REG_EDX, 4)
        and initialize.mnemonic == "call"
        and len(initialize.operands) == 1
        and initialize.operands[0].type == capstone_x86.X86_OP_IMM
    ):
        return None
    slot = exact_rip_target(exchange, exchange.operands[0], 8)
    plus8 = exact_rip_target(lea, lea.operands[1], 8)
    published = exact_rip_target(publish, publish.operands[0], 8)
    context_target = exact_rip_target(context, context.operands[1], 8)
    label_target = exact_rip_target(label, label.operands[1], 8)
    if (
        slot is None
        or slot % 8
        or plus8 != slot + 8
        or published != slot + 8
        or context_target != slot + 0x10
        or label_target is None
    ):
        return None
    try:
        image.require_code_rva(initialize.operands[0].imm, 1)
    except AnalyzerError:
        return None
    rodata = image.sections.get(".rodata")
    if (
        rodata is None
        or rodata.section_type != SHT_PROGBITS
        or rodata.flags & SHF_ALLOC == 0
        or rodata.flags & (SHF_WRITE | SHF_EXECINSTR)
        or label_target < rodata.address
        or label_target + 1 > rodata.address + rodata.size
    ):
        return None
    clear = instructions[9]
    if not (
        clear.mnemonic == "xorps"
        and len(clear.operands) == 2
        and is_register(clear.operands[0], capstone_x86.X86_REG_XMM0, 16)
        and is_register(clear.operands[1], capstone_x86.X86_REG_XMM0, 16)
    ):
        return None
    for index, offset in ((10, 0x28), (11, 0x18)):
        zero = instructions[index]
        if not (
            zero.mnemonic == "movups"
            and len(zero.operands) == 2
            and exact_rip_target(zero, zero.operands[0], 16) == slot + offset
            and is_register(zero.operands[1], capstone_x86.X86_REG_XMM0, 16)
        ):
            return None
    if not (
        is_jni_immediate_store(instructions[12], 4, slot + 0x38, 0x3F800000)
        and instructions[13].mnemonic == "pop"
        and len(instructions[13].operands) == 1
        and is_register(instructions[13].operands[0], capstone_x86.X86_REG_RBP, 8)
        and instructions[14].mnemonic == "ret"
        and not instructions[14].operands
    ):
        return None
    try:
        image.require_writable_rva(slot, 0x40)
    except AnalyzerError:
        return None
    return slot


def analyze_jni_singleton_initializer(
    image: ElfImage, initializer: int
) -> tuple[int, tuple[Any, ...], int]:
    # The legacy layout has an established data contract. A helper-shaped
    # initializer remains unsupported until its call target and side effects
    # have been reviewed.
    try:
        legacy = disassemble(image, initializer, 11)
        legacy_slot = match_legacy_jni_singleton_layout(image, legacy)
        if legacy_slot is not None:
            return legacy_slot, tuple(legacy), 0x400
    except AnalyzerError:
        pass
    helper = disassemble(image, initializer, 15)
    helper_slot = match_helper_jni_singleton_layout(image, helper)
    if helper_slot is not None:
        raise AnalyzerError(
            "JNI singleton helper-shaped layout is unsupported/unreviewed"
        )
    raise AnalyzerError(
        "JNI singleton initializer does not match a supported data layout"
    )


def derive_jni_singleton_signature_match(
    reference: ElfImage,
    candidate: ElfImage,
    reference_init: Sequence[int],
    candidate_init: Sequence[int],
    reference_profile: dict[str, Any],
    anchors: dict[str, Any],
) -> tuple[SignatureMatch, int, int]:
    reference_initializer = parse_rva(
        anchors["jni_singleton_initializer_rva"], "JNI singleton anchor"
    )
    if (
        len(reference_init) <= 5
        or len(candidate_init) <= 5
        or reference_init[5] != reference_initializer
        or candidate_init.count(candidate_init[5]) != 1
        or reference_init.count(reference_initializer) != 1
    ):
        raise AnalyzerError(
            "JNI singleton initializer is not a unique constructor index 5"
        )
    reference_slot = parse_rva(
        reference_profile["data_seeds"]["jni_singleton_slot"],
        "reference JNI singleton slot",
    )
    old_slot, old_instructions, old_seed_bytes = analyze_jni_singleton_initializer(
        reference, reference_initializer
    )
    if (
        old_slot != reference_slot
        or reference_profile["data_seeds"].get("jni_singleton_bytes")
        != old_seed_bytes
    ):
        raise AnalyzerError(
            "reference JNI singleton slot or seed size differs from its exact profile"
        )
    new_slot, new_instructions, new_seed_bytes = analyze_jni_singleton_initializer(
        candidate, candidate_init[5]
    )
    return (
        SignatureMatch(candidate_init[5], old_instructions, new_instructions),
        new_slot,
        new_seed_bytes,
    )


def derive_registry_slot(candidate: ElfImage, initializer_rva: int) -> int:
    # The initializer publishes its newly allocated registry immediately
    # before its first return. Do not infer that slot from adjacent functions:
    # future link layouts may place unrelated registry users after this body.
    candidate.require_code_rva(initializer_rva)
    offset = candidate.rva_to_offset(initializer_rva)
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    decoder.detail = True
    window = candidate.bytes_at(
        offset,
        min(REGISTRY_INITIALIZER_MAX_BYTES, candidate.file_size - offset),
        "registry initializer",
    )
    stores = []
    expected_address = initializer_rva
    found_return = False
    for instruction in decoder.disasm(window, initializer_rva):
        if instruction.address != expected_address or instruction.size <= 0:
            raise AnalyzerError("registry initializer disassembly is not contiguous")
        expected_address += instruction.size
        operands = instruction.operands
        if (
            instruction.mnemonic == "mov"
            and len(operands) == 2
            and operands[0].type == capstone_x86.X86_OP_MEM
            and operands[0].mem.base == capstone_x86.X86_REG_RIP
            and operands[0].size == 8
            and operands[1].type == capstone_x86.X86_OP_REG
            and operands[1].reg == capstone_x86.X86_REG_RBX
        ):
            stores.append(instruction.address + instruction.size + operands[0].mem.disp)
        if instruction.group(capstone.CS_GRP_RET):
            found_return = True
            break
    if not found_return:
        raise AnalyzerError("pre-JNI registry initializer has no bounded return")
    if len(stores) != 1:
        raise AnalyzerError(
            "pre-JNI registry initializer does not publish one unique slot"
        )
    slot = stores[0]
    if slot % 8:
        raise AnalyzerError("pre-JNI registry slot is not pointer-aligned")
    candidate.require_writable_rva(slot, 8)
    return slot


def validate_reference_registry_slot(
    reference: ElfImage, bootstrap: dict[str, Any]
) -> None:
    initializer = parse_rva(
        bootstrap["registry_initializer"], "reference registry initializer"
    )
    expected_slot = parse_rva(bootstrap["registry_slot"], "reference registry slot")
    if derive_registry_slot(reference, initializer) != expected_slot:
        raise AnalyzerError(
            "reference registry slot does not match its ELF initializer"
        )


def bridge_map(profile: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {entry["label"]: entry for entry in profile["bridge_entries"]}


def create_signature_specs(
    profile: dict[str, Any], anchors: dict[str, Any]
) -> list[SignatureSpec]:
    bridges = bridge_map(profile)
    data_seeds = profile["data_seeds"]
    bootstrap = profile["native_pre_jni_bootstrap"]
    constructor_rvas = anchors["constructor_rvas"]
    return [
        SignatureSpec(
            "small-allocate",
            parse_rva(bridges["small-allocate"]["rva"], "small allocate"),
            24,
        ),
        SignatureSpec(
            # This bridge is an 18-instruction leaf.  Keeping the signature
            # bounded to the leaf avoids coupling it to padding or the next
            # function, which changed from executable code to INT3 padding in
            # Roblox versionCode 2904.
            "usable-size",
            parse_rva(bridges["usable-size"]["rva"], "usable size"),
            18,
        ),
        SignatureSpec(
            "reallocate", parse_rva(bridges["reallocate"]["rva"], "reallocate"), 24
        ),
        SignatureSpec(
            "allocate", parse_rva(bridges["allocate"]["rva"], "allocate"), 24
        ),
        SignatureSpec(
            "aligned-allocate-direct",
            parse_rva(bridges["aligned-allocate-direct"]["rva"], "aligned allocate"),
            24,
        ),
        SignatureSpec("free", parse_rva(bridges["free"]["rva"], "free"), 24),
        SignatureSpec(
            "arena-initializer",
            parse_rva(data_seeds["arena_initializer"], "arena initializer"),
            24,
        ),
        SignatureSpec(
            "allocator-thread-initializer",
            parse_rva(
                data_seeds["allocator_thread_initializer"], "thread initializer"
            ),
            # The wrapper ends at instruction 19. Later instructions belong
            # to an unrelated adjacent initializer.
            19,
        ),
        SignatureSpec(
            "registry-initializer",
            parse_rva(bootstrap["registry_initializer"], "registry initializer"),
            20,
            True,
        ),
        SignatureSpec(
            "allocator-object-initializer",
            parse_rva(
                anchors["allocator_object_initializer_rva"], "allocator object anchor"
            ),
            19,
        ),
        SignatureSpec(
            "empty-string-initializer",
            parse_rva(anchors["empty_string_initializer_rva"], "empty string anchor"),
            23,
        ),
        SignatureSpec(
            "constructor-2", parse_rva(constructor_rvas["2"], "constructor 2"), 24
        ),
        SignatureSpec(
            "constructor-3", parse_rva(constructor_rvas["3"], "constructor 3"), 24
        ),
        SignatureSpec(
            "constructor-4",
            parse_rva(constructor_rvas["4"], "constructor 4"),
            24,
            minimum_anchor_bytes=3,
        ),
    ]


def adjusted_ranges(
    ranges: list[dict[str, int]], reference_count: int, candidate_count: int
) -> list[dict[str, int]]:
    result = []
    for item in ranges:
        end = (
            candidate_count
            if item["end_exclusive"] == reference_count
            else item["end_exclusive"]
        )
        if end > candidate_count:
            raise AnalyzerError(
                "reference constructor range cannot fit candidate init-array"
            )
        result.append({"begin": item["begin"], "end_exclusive": end})
    return validated_ranges(result, "derived constructor ranges", candidate_count)


def derive_profile(
    reference: ElfImage,
    candidate: ElfImage,
    reference_sidecar: dict[str, Any],
) -> tuple[dict[str, Any], dict[str, Any]]:
    require_capstone()
    if (
        reference.build_id != reference_sidecar["elf_build_id"]
        or reference.sha256 != reference_sidecar["payload_sha256"]
    ):
        raise AnalyzerError(
            "reference ELF bytes do not match the exact sidecar identity"
        )
    if candidate.build_id == reference.build_id or candidate.sha256 == reference.sha256:
        raise AnalyzerError("candidate must be a distinct Roblox payload")
    reference.validate_toolchain()
    candidate.validate_toolchain()
    missing_exports = REQUIRED_EXPORTS - candidate.exported_functions().keys()
    if missing_exports:
        raise AnalyzerError(
            "candidate is missing required JNI exports: "
            + ", ".join(sorted(missing_exports))
        )

    reference_profile = reference_sidecar["profile"]
    anchors = reference_sidecar["derivation_anchors"]
    reference_init = reference.init_array_relocations()
    candidate_init = candidate.init_array_relocations()
    if len(reference_init) != reference_profile["init_array_count"]:
        raise AnalyzerError(
            "reference sidecar init-array count differs from ELF metadata"
        )
    if len(candidate_init) < 8:
        raise AnalyzerError("candidate init-array is unexpectedly short")
    matches = {}
    allocate_matches = ()
    for spec in create_signature_specs(reference_profile, anchors):
        if spec.name == "allocate":
            allocate_matches = find_signature_matches(
                reference, candidate, spec, allow_multiple_candidates=True
            )
        elif spec.name == "registry-initializer":
            matches[spec.name] = find_registry_signature_match(
                reference,
                candidate,
                spec,
                parse_rva(
                    reference_profile["native_pre_jni_bootstrap"]["registry_slot"],
                    "reference registry slot",
                ),
                allocate_matches,
            )
        elif spec.name == "constructor-4":
            matches[spec.name] = find_constructor_boundary_match(
                reference,
                candidate,
                spec,
                reference_init[4],
                candidate_init[4],
            )
        else:
            matches[spec.name] = find_signature_match(reference, candidate, spec)

    (
        singleton_match,
        jni_singleton_slot,
        jni_singleton_bytes,
    ) = derive_jni_singleton_signature_match(
        reference,
        candidate,
        reference_init,
        candidate_init,
        reference_profile,
        anchors,
    )
    matches["jni-singleton-initializer"] = singleton_match

    # The allocate signature alone is intentionally ambiguous in 2904. Resolve
    # it through an independent invariant: the registry initializer directly
    # calls the allocator wrapper that Aurora must publish in the profile.
    registry_match = matches["registry-initializer"]
    reference_allocate = parse_rva(
        bridge_map(reference_profile)["allocate"]["rva"],
        "reference allocate",
    )
    if (
        direct_call_target(
            registry_match.reference_instructions[9],
            "reference registry allocator call",
        )
        != reference_allocate
    ):
        raise AnalyzerError(
            "reference registry initializer does not call its allocator bridge"
        )
    candidate_allocate = direct_call_target(
        registry_match.candidate_instructions[9],
        "candidate registry allocator call",
    )
    selected_allocate = tuple(
        match for match in allocate_matches if match.rva == candidate_allocate
    )
    if len(selected_allocate) != 1:
        raise AnalyzerError(
            "signature allocate candidates do not identify exactly one "
            "registry allocator call"
        )
    matches["allocate"] = selected_allocate[0]
    for index in (2, 3, 4):
        match = matches[f"constructor-{index}"]
        if (
            reference_init[index] != match.reference_instructions[0].address
            or candidate_init[index] != match.rva
        ):
            raise AnalyzerError(
                f"constructor {index} moved outside its verified init-array boundary"
            )
    singleton_match = matches["jni-singleton-initializer"]
    if (
        reference_init[5]
        != parse_rva(
            anchors["jni_singleton_initializer_rva"], "JNI singleton anchor"
        )
        or candidate_init[5] != singleton_match.rva
    ):
        raise AnalyzerError("JNI singleton initializer is not constructor index 5")

    candidate_bridge_entries = []
    for entry in reference_profile["bridge_entries"]:
        candidate_bridge_entries.append(
            {
                "rva": format_rva(matches[entry["label"]].rva),
                "kind": entry["kind"],
                "label": entry["label"],
            }
        )
    candidate_bridge_map = {entry["label"]: entry for entry in candidate_bridge_entries}

    reference_seeds = reference_profile["data_seeds"]
    allocator_slot = mapped_rip_target(
        matches["allocator-object-initializer"],
        parse_rva(reference_seeds["allocator_object_slot"], "allocator object slot"),
        "allocator object slot",
    )
    empty_string_slot = mapped_rip_target(
        matches["empty-string-initializer"],
        parse_rva(reference_seeds["empty_string_slot"], "empty string slot"),
        "empty string slot",
    )
    arena_guard_slot = mapped_rip_target(
        matches["arena-initializer"],
        parse_rva(reference_seeds["arena_guard_slot"], "arena guard slot"),
        "arena guard slot",
    )
    arena_table_slot = mapped_rip_target(
        matches["usable-size"],
        parse_rva(reference_seeds["arena_table_slot"], "arena table slot"),
        "arena table slot",
    )
    for slot in (
        allocator_slot,
        empty_string_slot,
        jni_singleton_slot,
        arena_guard_slot,
        arena_table_slot,
    ):
        candidate.require_writable_rva(slot, 8)

    validate_reference_registry_slot(
        reference, reference_profile["native_pre_jni_bootstrap"]
    )

    registry_initializer = matches["registry-initializer"].rva
    registry_slot = derive_registry_slot(candidate, registry_initializer)
    allocate_rva = parse_rva(
        candidate_bridge_map["allocate"]["rva"], "derived allocate"
    )
    free_rva = parse_rva(candidate_bridge_map["free"]["rva"], "derived free")
    registry_call = matches["registry-initializer"].candidate_instructions[9]
    if (
        len(registry_call.operands) != 1
        or registry_call.operands[0].type != capstone_x86.X86_OP_IMM
        or registry_call.operands[0].imm != allocate_rva
    ):
        raise AnalyzerError(
            "registry initializer no longer calls the derived native allocator"
        )

    init_section = candidate.sections[".init_array"]
    reference_count = reference_profile["init_array_count"]
    candidate_count = len(candidate_init)
    constructor_ranges = adjusted_ranges(
        validated_ranges(
            reference_profile["constructor_run_ranges"],
            "reference constructor ranges",
            reference_count,
        ),
        reference_count,
        candidate_count,
    )
    native_ranges = adjusted_ranges(
        validated_ranges(
            reference_profile["native_mimalloc_constructor_run_ranges"],
            "reference native constructor ranges",
            reference_count,
        ),
        reference_count,
        candidate_count,
    )
    thread_boundary = reference_profile[
        "native_mimalloc_thread_initializer_after_constructor"
    ]
    if thread_boundary != 2 or not any(
        item["begin"] <= thread_boundary < item["end_exclusive"]
        for item in native_ranges
    ):
        raise AnalyzerError("native allocator TLS boundary is no longer verified")

    profile = {
        "elf_build_id": candidate.build_id,
        "bridge_entries": candidate_bridge_entries,
        "data_seeds": {
            "allocator_object_slot": format_rva(allocator_slot),
            "empty_string_slot": format_rva(empty_string_slot),
            "jni_singleton_slot": format_rva(jni_singleton_slot),
            "jni_singleton_bytes": jni_singleton_bytes,
            "arena_initializer": format_rva(matches["arena-initializer"].rva),
            "allocator_thread_initializer": format_rva(
                matches["allocator-thread-initializer"].rva
            ),
            "arena_guard_slot": format_rva(arena_guard_slot),
            "arena_table_slot": format_rva(arena_table_slot),
            "arena_table_slot_count": reference_seeds["arena_table_slot_count"],
        },
        "native_allocator": {
            "allocate": format_rva(allocate_rva),
            "deallocate": format_rva(free_rva),
        },
        "init_array_offset": format_rva(init_section.address),
        "init_array_count": candidate_count,
        "constructor_run_ranges": constructor_ranges,
        "native_mimalloc_constructor_run_ranges": native_ranges,
        "native_mimalloc_thread_initializer_after_constructor": thread_boundary,
        "native_pre_jni_bootstrap": {
            "registry_initializer": format_rva(registry_initializer),
            "registry_slot": format_rva(registry_slot),
        },
        "default_allocator_strategy": "native_mimalloc",
    }
    derived_anchors = {
        "signature_version": 1,
        "allocator_object_initializer_rva": format_rva(
            matches["allocator-object-initializer"].rva
        ),
        "empty_string_initializer_rva": format_rva(
            matches["empty-string-initializer"].rva
        ),
        "jni_singleton_initializer_rva": format_rva(singleton_match.rva),
        "constructor_rvas": {
            "2": format_rva(candidate_init[2]),
            "3": format_rva(candidate_init[3]),
            "4": format_rva(candidate_init[4]),
            "5": format_rva(candidate_init[5]),
        },
    }
    validate_profile_shape(profile, derived_anchors)
    return profile, derived_anchors


def output_documents(
    candidate: ElfImage,
    metadata: dict[str, Any],
    reference_sidecar: dict[str, Any],
    profile: dict[str, Any],
    anchors: dict[str, Any],
    runtime_compatibility: dict[str, Any] | None = None,
) -> tuple[dict[str, Any], dict[str, Any]]:
    version_code = metadata["version_code"]
    payload_id = f"{version_code}-{candidate.build_id}"
    sidecar = {
        "schema_version": 1,
        "elf_build_id": candidate.build_id,
        "payload_sha256": candidate.sha256,
        "payload_id": payload_id,
        "payload_path": f"payloads/{payload_id}",
        "reference": {
            "elf_build_id": reference_sidecar["elf_build_id"],
            "payload_sha256": reference_sidecar["payload_sha256"],
        },
        "profile": profile,
        "derivation_anchors": anchors,
    }
    compatibility_profile = {
        "version_name": metadata["version_name"],
        "version_code": version_code,
        "elf_build_id": candidate.build_id,
        "status": "experimental",
        "default_allowed": True,
        "allow_legacy_binary_patches": False,
        "allow_host_abi_bridges": True,
        "allow_host_constructor_replay": True,
        "reason": (
            "Machine-derived exact-Build-ID profile for isolated "
            "probation only; normal activation requires two "
            "successful no-recovery Tier C canaries."
        ),
    }
    if runtime_compatibility:
        compatibility_profile.update(runtime_compatibility)
    manifest = {
        "schema_version": 1,
        "profiles": [compatibility_profile],
    }
    return sidecar, manifest


def encode_json(document: dict[str, Any]) -> bytes:
    return (json.dumps(document, indent=2, sort_keys=True) + "\n").encode("utf-8")


def write_json_atomic(destination: str, document: dict[str, Any]) -> None:
    encoded = encode_json(document)
    if destination == "-":
        sys.stdout.buffer.write(encoded)
        sys.stdout.buffer.flush()
        return
    path = Path(destination)
    parent = path.parent
    if not parent.is_dir() or parent.is_symlink():
        raise AnalyzerError(f"output parent must be a non-symlink directory: {parent}")
    temporary_path = None
    try:
        descriptor, temporary_name = tempfile.mkstemp(
            prefix=f".{path.name}.", dir=parent
        )
        temporary_path = Path(temporary_name)
        os.fchmod(descriptor, 0o600)
        with os.fdopen(descriptor, "wb") as output:
            output.write(encoded)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary_path, path)
        temporary_path = None
    except OSError as error:
        raise AnalyzerError(f"cannot atomically write output: {path}") from error
    finally:
        if temporary_path is not None:
            try:
                temporary_path.unlink()
            except FileNotFoundError:
                pass


def analyze(arguments: argparse.Namespace) -> tuple[dict[str, Any], dict[str, Any]]:
    reference_path = Path(arguments.reference_lib)
    candidate_path = Path(arguments.candidate_lib)
    metadata_path = Path(arguments.payload_metadata)
    sidecar_path = Path(arguments.reference_profile)
    reference_sidecar = validated_sidecar(sidecar_path)
    with ElfImage(reference_path) as reference, ElfImage(candidate_path) as candidate:
        metadata = validated_payload_metadata(metadata_path, candidate_path, candidate)
        profile, anchors = derive_profile(reference, candidate, reference_sidecar)
        runtime_compatibility = derive_runtime_compatibility(
            reference,
            candidate,
            tuple(Path(path) for path in arguments.reference_compatibility),
        )
        return output_documents(
            candidate,
            metadata,
            reference_sidecar,
            profile,
            anchors,
            runtime_compatibility,
        )


def parse_arguments(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Derive a probationary Roblox HostAbi sidecar"
    )
    parser.add_argument("--reference-lib", required=True, metavar="REF")
    parser.add_argument("--reference-profile", required=True, metavar="REFJSON")
    parser.add_argument(
        "--reference-compatibility",
        action="append",
        default=[],
        metavar="MANIFEST",
        help="compatibility manifest containing exact reference runtime anchors",
    )
    parser.add_argument("--candidate-lib", required=True, metavar="LIB")
    parser.add_argument("--payload-metadata", required=True, metavar="META")
    parser.add_argument("--output", required=True, metavar="PROFILE")
    parser.add_argument("--compatibility-output", required=True, metavar="MANIFEST")
    arguments = parser.parse_args(argv)
    if arguments.output == "-" and arguments.compatibility_output == "-":
        parser.error("only one output may use stdout")
    if (
        arguments.output != "-"
        and arguments.compatibility_output != "-"
        and Path(arguments.output).resolve()
        == Path(arguments.compatibility_output).resolve()
    ):
        parser.error("profile and compatibility outputs must differ")
    return arguments


def main(argv: Sequence[str] | None = None) -> int:
    arguments = parse_arguments(sys.argv[1:] if argv is None else argv)
    try:
        sidecar, manifest = analyze(arguments)
        write_json_atomic(arguments.output, sidecar)
        write_json_atomic(arguments.compatibility_output, manifest)
    except AnalyzerError as error:
        print(f"derive-host-abi-profile: error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
