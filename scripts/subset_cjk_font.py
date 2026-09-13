#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Subset a CJK font to UI strings + XOR TALK.GRP + RANGER/WAR names.

Output is gitignored. TALK.GRP is bitwise-not Big5; RANGER names and WAR.STA
are plain Big5 fields. Do not XOR RANGER.GRP.
"""

from __future__ import annotations

import pathlib
import struct
import sys

CHAR_SIZE = 182
ITEM_SIZE = 190
SKILL_SIZE = 136
SUBMAP_SIZE = 52
WAR_SIZE = 186


def add_printable(chars: set[str], text: str) -> None:
    for ch in text:
        if ch.isprintable() and ord(ch) < 0x10000:
            chars.add(ch)


def big5_field(raw: bytes) -> str:
    payload = raw.split(b"\x00", 1)[0]
    if not payload:
        return ""
    return payload.decode("big5", errors="ignore")


def load_grp_records(grp_path: pathlib.Path) -> list[bytes]:
    idx_path = grp_path.with_name(grp_path.stem + ".IDX")
    idx = idx_path.read_bytes()
    grp = grp_path.read_bytes()
    ends = struct.unpack(f"<{len(idx) // 4}I", idx)
    records: list[bytes] = []
    offset = 0
    for end in ends:
        if end == 0:
            end = len(grp)
        records.append(grp[offset:end])
        offset = end
    return records


def add_ranger_names(chars: set[str], grp_path: pathlib.Path) -> None:
    records = load_grp_records(grp_path)
    if len(records) < 5:
        raise SystemExit(f"RANGER.GRP needs 6 records, got {len(records)}")
    char_blob, item_blob, submap_blob, skill_blob = records[1:5]
    if len(char_blob) % CHAR_SIZE:
        raise SystemExit(f"unexpected CharacterData size in {grp_path}")
    if len(item_blob) % ITEM_SIZE:
        raise SystemExit(f"unexpected ItemData size in {grp_path}")
    if len(submap_blob) % SUBMAP_SIZE:
        raise SystemExit(f"unexpected SubMapData size in {grp_path}")
    if len(skill_blob) % SKILL_SIZE:
        raise SystemExit(f"unexpected SkillData size in {grp_path}")
    for i in range(0, len(char_blob), CHAR_SIZE):
        block = char_blob[i : i + CHAR_SIZE]
        add_printable(chars, big5_field(block[8:18]))
        add_printable(chars, big5_field(block[18:28]))
    for i in range(0, len(item_blob), ITEM_SIZE):
        block = item_blob[i : i + ITEM_SIZE]
        add_printable(chars, big5_field(block[2:22]))
        add_printable(chars, big5_field(block[22:42]))
        add_printable(chars, big5_field(block[42:72]))
    for i in range(0, len(submap_blob), SUBMAP_SIZE):
        add_printable(chars, big5_field(submap_blob[i + 2 : i + 12]))
    for i in range(0, len(skill_blob), SKILL_SIZE):
        add_printable(chars, big5_field(skill_blob[i + 2 : i + 12]))


def add_war_names(chars: set[str], sta_path: pathlib.Path) -> None:
    raw = sta_path.read_bytes()
    if len(raw) % WAR_SIZE:
        raise SystemExit(f"unexpected WarfieldInfo size in {sta_path}")
    for i in range(0, len(raw), WAR_SIZE):
        add_printable(chars, big5_field(raw[i + 2 : i + 12]))


def collect_chars(paths: list[pathlib.Path]) -> str:
    chars = set(chr(c) for c in range(0x20, 0x7F))
    chars.update("，。！？、；：「」『』（）【】—…·《》○●★☆")
    for path in paths:
        name = path.name.upper()
        if path.suffix.lower() in {".toml", ".txt", ".md"}:
            add_printable(chars, path.read_bytes().decode("utf-8"))
        elif name == "RANGER.GRP":
            add_ranger_names(chars, path)
        elif name == "WAR.STA":
            add_war_names(chars, path)
        else:
            payload = path.read_bytes()
            if name == "TALK.GRP" or path.suffix.lower() == ".grp":
                # HOJY TALK.GRP bytes are bitwise-not Big5.
                payload = bytes((~b) & 0xFF for b in payload)
            add_printable(chars, payload.decode("big5", errors="ignore"))
            add_printable(chars, payload.decode("utf-8", errors="ignore"))
    return "".join(sorted(chars))


def main() -> int:
    if len(sys.argv) < 4:
        print(
            "Usage: subset_cjk_font.py <src-font> <strings.toml> <out-otf> [extra-text...]",
            file=sys.stderr,
        )
        return 2
    src = pathlib.Path(sys.argv[1])
    strings = pathlib.Path(sys.argv[2])
    out = pathlib.Path(sys.argv[3])
    extras = [pathlib.Path(p) for p in sys.argv[4:]]
    if not src.is_file():
        print(f"font not found: {src}", file=sys.stderr)
        return 1
    try:
        from fontTools.subset import Subsetter, Options
        from fontTools.ttLib import TTFont
    except ImportError:
        print("fontTools is not installed in this Python.", file=sys.stderr)
        return 1

    text = collect_chars([strings, *extras])
    font = TTFont(src, fontNumber=0)
    options = Options()
    options.flavor = None
    options.desubroutinize = True
    subsetter = Subsetter(options=options)
    subsetter.populate(text=text)
    subsetter.subset(font)
    out.parent.mkdir(parents=True, exist_ok=True)
    font.save(out)
    print(f"subset {len(text)} chars -> {out} ({out.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
