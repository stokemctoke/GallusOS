#!/usr/bin/env python3
"""Run GallusOS host-side tests without pytest."""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TEST_FILE = ROOT / "tests" / "host" / "test_manifest_gen.py"


def load_tests():
    spec = importlib.util.spec_from_file_location("test_manifest_gen", TEST_FILE)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def main() -> int:
    module = load_tests()
    tests = [
        name
        for name in dir(module)
        if name.startswith("test_") and callable(getattr(module, name))
    ]
    failed = 0
    for name in tests:
        fn = getattr(module, name)
        try:
            fn()
            print(f"PASS {name}")
        except AssertionError as exc:
            failed += 1
            print(f"FAIL {name}: {exc}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
