#!/usr/bin/env python3
"""Rewrite HOJY config.toml window/ui/audio for Tab5. Does not touch game binaries."""

from __future__ import annotations

import pathlib
import sys


PATCHES = {
    "main": {
        "save_path": 'save_path = "save"',
        "pre_path": 'pre_path = "/sdcard/jinyong/"',
    },
    "window": {
        "width": "width = 640",
        "height": "height = 480",
        "limit_fps": "limit_fps = 30",
    },
    "ui": {
        "no_name_input": "no_name_input = true",
        "show_minimap": "show_minimap = false",
        "show_map_mini_panel": "show_map_mini_panel = false",
        "scale": "scale = 2.0",
    },
    "audio": {
        "sample_rate": "sample_rate = 22050",
    },
}


def patch(text: str) -> str:
    lines = text.splitlines(keepends=True)
    section = ""
    seen: set[tuple[str, str]] = set()
    out: list[str] = []
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            section = stripped[1:-1]
            out.append(line)
            continue
        key = stripped.split("=", 1)[0].strip() if "=" in stripped else ""
        if section in PATCHES and key in PATCHES[section]:
            nl = "\n" if line.endswith("\n") else ""
            out.append(PATCHES[section][key] + nl)
            seen.add((section, key))
        else:
            out.append(line)
    missing = [
        (sec, key)
        for sec, keys in PATCHES.items()
        for key in keys
        if (sec, key) not in seen
    ]
    if missing:
        by_sec: dict[str, list[str]] = {}
        for sec, key in missing:
            by_sec.setdefault(sec, []).append(key)
        rebuilt: list[str] = []
        section = ""
        for line in out:
            stripped = line.strip()
            if stripped.startswith("[") and stripped.endswith("]"):
                if section in by_sec:
                    for key in by_sec[section]:
                        rebuilt.append(PATCHES[section][key] + "\n")
                    del by_sec[section]
                section = stripped[1:-1]
            rebuilt.append(line)
        if section in by_sec:
            for key in by_sec[section]:
                rebuilt.append(PATCHES[section][key] + "\n")
            del by_sec[section]
        if by_sec:
            raise SystemExit("cannot patch missing sections: " + ", ".join(by_sec))
        out = rebuilt
    return "".join(out)


def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: patch_tab5_config.py <config.toml>", file=sys.stderr)
        return 2
    path = pathlib.Path(sys.argv[1])
    path.write_text(patch(path.read_text(encoding="utf-8")), encoding="utf-8")
    print(f"patched Tab5 device settings: {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
