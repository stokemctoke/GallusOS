"""Host-side tests for GallusOS tooling."""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST_GEN = ROOT / "tools" / "gallus_manifest_gen.py"
MODULES = ROOT / "modules"


def run_manifest_gen(manifest: Path, component: str, output: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            sys.executable,
            str(MANIFEST_GEN),
            "--manifest",
            str(manifest),
            "--component",
            component,
            "--output",
            str(output),
        ],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )


def test_all_module_manifests_validate() -> None:
    for module_dir in sorted(MODULES.iterdir()):
        manifest = module_dir / "manifest.json"
        if not manifest.exists():
            continue
        out = module_dir / "_test.gen.cpp"
        result = run_manifest_gen(manifest, module_dir.name, out)
        try:
            assert result.returncode == 0, result.stderr or result.stdout
            assert "ModuleRegistrar" in out.read_text(encoding="utf-8")
        finally:
            if out.exists():
                out.unlink()


def test_rejects_bad_module_name() -> None:
    bad = ROOT / "tests" / "host" / "_bad_manifest.json"
    bad.write_text(
        json.dumps(
            {
                "name": "Bad-Name",
                "version": "0.1.0",
                "description": "bad",
                "author": "test",
                "license": "PolyForm-Perimeter-1.0.0",
                "category": "Examples",
            }
        ),
        encoding="utf-8",
    )
    out = ROOT / "tests" / "host" / "_bad.gen.cpp"
    result = run_manifest_gen(bad, "bad", out)
    try:
        assert result.returncode != 0
    finally:
        bad.unlink(missing_ok=True)
        out.unlink(missing_ok=True)
