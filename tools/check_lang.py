#!/usr/bin/env python3
"""Checks every TR("English", "Nederlands") in native/: both texts must have the same placeholders
(%s, %d, ...), in the same order, because they are used as printf formats.
  python3 tools/check_lang.py"""
import re
import sys
from pathlib import Path

NATIVE = Path(__file__).resolve().parents[1] / "native"
PAIR = re.compile(r'TR\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\)')
SPEC = re.compile(r"%[-+ #0-9.]*[a-zA-Z%]+")
bad = count = 0
for path in sorted(NATIVE.glob("*.[ch]*")):
    for m in PAIR.finditer(path.read_text()):
        count += 1
        if SPEC.findall(m.group(1)) != SPEC.findall(m.group(2)):
            bad += 1
            print(f"{path.name}: {m.group(1)!r} / {m.group(2)!r}")
print(f"{count} translations, {bad} with different placeholders")
sys.exit(1 if bad else 0)
