#!/usr/bin/env python3
"""Ensure librflow_stream_stats_* in librflow_common.h matches implementations in stream_stats.cpp."""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

NAME_RE = re.compile(r"\b(librflow_stream_stats(?:_(?:retain|release|get_[a-z0-9_]+)))\s*\(")


def names_in_cpp(text: str) -> set[str]:
    out: set[str] = set()
    in_extern_c = False
    for raw in text.splitlines():
        s = raw.strip()
        if s.startswith('extern "C"') and not s.endswith("};"):
            in_extern_c = True
            continue
        if in_extern_c and "// extern" in raw and raw.strip().startswith("}"):
            break
        if in_extern_c:
            for m in NAME_RE.finditer(raw):
                out.add(m.group(1))
    return out


def names_from_header(lines: list[str]) -> set[str]:
    names: set[str] = set()
    i = 0
    while i < len(lines):
        chunk = lines[i]
        advance = i + 1
        while i + 1 < len(lines):
            stripped = lines[i].rstrip()
            if stripped.endswith("\\"):
                chunk += "\n" + lines[i + 1]
                i += 1
                advance = i + 1
                continue
            break
        if "LIBRFLOW_API_EXPORT" in chunk and "librflow_stream_stats_" in chunk:
            for m in NAME_RE.finditer(chunk.replace("\t", " ")):
                names.add(m.group(1))
        i = advance
    return names


def main() -> int:
    hdr_lines = (ROOT / "include" / "rflow" / "librflow_common.h").read_text(encoding="utf-8").splitlines()
    cpp_txt = (ROOT / "src" / "common" / "media" / "stream_stats.cpp").read_text(encoding="utf-8")
    h_syms = names_from_header(hdr_lines)
    c_syms = names_in_cpp(cpp_txt)
    missing_impl = sorted(h_syms - c_syms)
    orphan_impl = sorted(c_syms - h_syms)
    if missing_impl or orphan_impl:
        if missing_impl:
            print("Declared with LIBRFLOW_API_EXPORT in librflow_common.h but missing in stream_stats.cpp:",
                  file=sys.stderr)
            for s in missing_impl:
                print(f"  - {s}", file=sys.stderr)
        if orphan_impl:
            print("Defined in stream_stats.cpp extern \"C\" but no matching LIBRFLOW_API_EXPORT declaration:",
                  file=sys.stderr)
            for s in orphan_impl:
                print(f"  - {s}", file=sys.stderr)
        return 1
    print(f"librflow_stream_stats ABI OK: {len(h_syms)} symbols")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
