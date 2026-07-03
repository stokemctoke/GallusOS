#!/usr/bin/env python3
"""GallusOS SDK command-line tool.

Wraps common developer workflows: scaffolding modules, validating
manifests, and thin wrappers around idf.py.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / "tools"
MANIFEST_GEN = TOOLS / "gallus_manifest_gen.py"
MODULE_CMAKE = TOOLS / "gallus_module.cmake"

MODULE_CPP_TEMPLATE = '''\
/// {description}

#include "gallus/log.hpp"
#include "gallus/sdk/module.hpp"

namespace {{

constexpr const char* kTag = "{name}";

class {class_name} : public gallus::sdk::Module {{
public:
    gallus::Status start() override {{
        gallus::Log::info(kTag, "{name} started");
        return gallus::Status::success();
    }}
}};

}}  // namespace

GALLUS_MODULE({class_name}, {name})
'''

MODULE_CMAKE_TEMPLATE = """\
include(${{CMAKE_CURRENT_LIST_DIR}}/../../tools/gallus_module.cmake)
gallus_module(SRCS module.cpp)
"""

MODULE_MANIFEST_TEMPLATE = {
    "name": "",
    "version": "0.1.0",
    "description": "",
    "author": "Gallus Gadgets",
    "license": "PolyForm-Perimeter-1.0.0",
    "category": "Examples",
    "menu_icon": "cube",
    "required_services": ["config", "scheduler"],
    "required_gpio": [],
    "events_published": [],
    "events_consumed": [],
    "capabilities": [],
}

MODULE_CONFIG_TEMPLATE = {}

MODULE_README_TEMPLATE = """\
# {name}

{description}

## Configuration

Namespace `{name}` in LittleFS config.
"""

SERVICE_HPP_TEMPLATE = '''\
#pragma once

#include "gallus/error.hpp"

namespace gallus::services {{

/// @brief {description}
class {class_name} {{
public:
    {class_name}() = default;
    {class_name}(const {class_name}&) = delete;
    {class_name}& operator=(const {class_name}&) = delete;

    Status init();
}};

}}  // namespace gallus::services
'''

SERVICE_CPP_TEMPLATE = '''\
#include "gallus/services/{name}_service.hpp"

#include "gallus/log.hpp"

namespace gallus::services {{

namespace {{
constexpr const char* kTag = "{tag}";
}}

Status {class_name}::init() {{
    gallus::Log::info(kTag, "ready");
    return gallus::Status::success();
}}

}}  // namespace gallus::services
'''

SERVICE_CMAKE_SNIPPET = '''\
        "src/{name}_service.cpp"
'''

DRIVER_HPP_TEMPLATE = '''\
#pragma once

#include "gallus/error.hpp"

namespace gallus::drivers {{

/// @brief {description}
class {class_name} {{
public:
    Status init();
}};

}}  // namespace gallus::drivers
'''

DRIVER_CPP_TEMPLATE = '''\
#include "gallus/drivers/{name}.hpp"

#include "gallus/log.hpp"

namespace gallus::drivers {{

namespace {{
constexpr const char* kTag = "{tag}";
}}

Status {class_name}::init() {{
    gallus::Log::info(kTag, "ready");
    return gallus::Status::success();
}}

}}  // namespace gallus::drivers
'''


def run_idf(args: list[str]) -> int:
    env = os.environ.copy()
    return subprocess.call(["idf.py", *args], cwd=ROOT, env=env)


def cmd_create_module(args: argparse.Namespace) -> int:
    name = args.name
    if not name.islower() or not name.replace("_", "").isalnum():
        print("error: module name must be lower_snake_case", file=sys.stderr)
        return 1

    module_dir = ROOT / "modules" / name
    if module_dir.exists():
        print(f"error: {module_dir} already exists", file=sys.stderr)
        return 1

    description = args.description or f"The {name} module."
    class_name = "".join(part.capitalize() for part in name.split("_")) + "Module"

    module_dir.mkdir(parents=True)
    manifest = dict(MODULE_MANIFEST_TEMPLATE)
    manifest["name"] = name
    manifest["description"] = description
    if args.category:
        manifest["category"] = args.category

    (module_dir / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (module_dir / "config.json").write_text(
        json.dumps({name: {}}, indent=2) + "\n", encoding="utf-8")
    (module_dir / "CMakeLists.txt").write_text(MODULE_CMAKE_TEMPLATE,
                                                 encoding="utf-8")
    (module_dir / "module.cpp").write_text(
        MODULE_CPP_TEMPLATE.format(name=name, class_name=class_name,
                                   description=description),
        encoding="utf-8")
    (module_dir / "README.md").write_text(
        MODULE_README_TEMPLATE.format(name=name, description=description),
        encoding="utf-8")

    print(f"created module at modules/{name}/")
    print("next: idf.py build")
    return 0


def cmd_create_service(args: argparse.Namespace) -> int:
    name = args.name
    if not name.islower() or not name.replace("_", "").isalnum():
        print("error: service name must be lower_snake_case", file=sys.stderr)
        return 1

    class_name = "".join(part.capitalize() for part in name.split("_")) + "Service"
    description = args.description or f"The {name} service."
    tag = name.replace("_", " ").title()

    hpp = ROOT / "components/gallus_services/include/gallus/services" / f"{name}_service.hpp"
    cpp = ROOT / "components/gallus_services/src" / f"{name}_service.cpp"
    if hpp.exists() or cpp.exists():
        print("error: service files already exist", file=sys.stderr)
        return 1

    hpp.write_text(SERVICE_HPP_TEMPLATE.format(
        name=name, class_name=class_name, description=description),
        encoding="utf-8")
    cpp.write_text(SERVICE_CPP_TEMPLATE.format(
        name=name, class_name=class_name, tag=tag),
        encoding="utf-8")

    print(f"created service stubs:")
    print(f"  {hpp.relative_to(ROOT)}")
    print(f"  {cpp.relative_to(ROOT)}")
    print(f"next: add {name}_service.cpp to components/gallus_services/CMakeLists.txt")
    return 0


def cmd_create_driver(args: argparse.Namespace) -> int:
    name = args.name
    if not name.islower() or not name.replace("_", "").isalnum():
        print("error: driver name must be lower_snake_case", file=sys.stderr)
        return 1

    class_name = "".join(part.capitalize() for part in name.split("_"))
    description = args.description or f"The {name} driver."
    tag = name.replace("_", " ").title()

    hpp = ROOT / "components/gallus_drivers/include/gallus/drivers" / f"{name}.hpp"
    cpp = ROOT / "components/gallus_drivers/src" / f"{name}.cpp"
    if hpp.exists() or cpp.exists():
        print("error: driver files already exist", file=sys.stderr)
        return 1

    hpp.write_text(DRIVER_HPP_TEMPLATE.format(
        name=name, class_name=class_name, description=description),
        encoding="utf-8")
    cpp.write_text(DRIVER_CPP_TEMPLATE.format(
        name=name, class_name=class_name, tag=tag),
        encoding="utf-8")

    print(f"created driver stubs:")
    print(f"  {hpp.relative_to(ROOT)}")
    print(f"  {cpp.relative_to(ROOT)}")
    print("next: add the .cpp to components/gallus_drivers/CMakeLists.txt")
    return 0


def cmd_validate_module(args: argparse.Namespace) -> int:
    module_dir = Path(args.path).resolve()
    if module_dir.is_file():
        manifest = module_dir
        component = module_dir.parent.name
        module_dir = module_dir.parent
    else:
        manifest = module_dir / "manifest.json"
        component = module_dir.name

    if not manifest.exists():
        print(f"error: no manifest at {manifest}", file=sys.stderr)
        return 1

    out = module_dir / "_validate.gen.cpp" if module_dir.is_dir() else Path("/tmp/_validate.gen.cpp")
    cmd = [
        sys.executable,
        str(MANIFEST_GEN),
        "--manifest",
        str(manifest),
        "--component",
        component,
        "--output",
        str(out),
    ]
    result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if out.exists():
        out.unlink()
    if result.returncode != 0:
        sys.stderr.write(result.stderr or result.stdout)
        return result.returncode

    if not getattr(args, "manifest_only", False):
        lint_rc = lint_module_dir(module_dir, quiet=True)
        if lint_rc != 0:
            return lint_rc

    print(f"manifest OK: {manifest}")
    return 0


MODULE_CONTEXT_SERVICES = frozenset({
    "config", "storage", "gpio", "rest", "i2c", "wifi",
})
METADATA_SERVICES = frozenset({"events", "scheduler", "logging"})


def lint_module_dir(module_dir: Path, *, quiet: bool = False) -> int:
    """Return 0 when the module directory passes structural lint."""
    name = module_dir.name
    errors: list[str] = []
    warnings: list[str] = []

    manifest_path = module_dir / "manifest.json"
    if not manifest_path.exists():
        errors.append("missing manifest.json")
        return report_lint(name, errors, warnings, quiet)

    for required in ("module.cpp", "CMakeLists.txt"):
        if not (module_dir / required).exists():
            errors.append(f"missing {required}")

    if not (module_dir / "config.json").exists():
        warnings.append("missing config.json example")

    if not (module_dir / "README.md").exists():
        warnings.append("missing README.md")

    cpp = module_dir / "module.cpp"
    if cpp.exists():
        text = cpp.read_text(encoding="utf-8")
        if f"GALLUS_MODULE(" not in text:
            errors.append("module.cpp missing GALLUS_MODULE macro")
        elif f"gallus_create_{name}" not in text and f", {name})" not in text:
            errors.append(f"GALLUS_MODULE name does not match directory '{name}'")

    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        errors.append(f"manifest.json invalid JSON: {exc}")
        return report_lint(name, errors, warnings, quiet)

    for svc in manifest.get("required_services", []):
        if svc in METADATA_SERVICES:
            continue
        if svc not in MODULE_CONTEXT_SERVICES:
            errors.append(
                f"required_services '{svc}' is not on ModuleContext v1 "
                f"(available: {', '.join(sorted(MODULE_CONTEXT_SERVICES))})")

    return report_lint(name, errors, warnings, quiet)


def report_lint(name: str, errors: list[str], warnings: list[str],
                quiet: bool) -> int:
    if not quiet:
        for msg in warnings:
            print(f"warn {name}: {msg}")
        for msg in errors:
            print(f"error {name}: {msg}", file=sys.stderr)
    if errors:
        return 1
    if not quiet and not warnings:
        print(f"lint OK: {name}")
    return 0


def cmd_lint(args: argparse.Namespace) -> int:
    modules = ROOT / "modules"
    failed = False
    targets: list[Path]
    if args.path:
        targets = [Path(args.path).resolve()]
    else:
        targets = [
            child for child in sorted(modules.iterdir())
            if child.is_dir() and (child / "manifest.json").exists()
        ]

    for module_dir in targets:
        if lint_module_dir(module_dir) != 0:
            failed = True
    return 1 if failed else 0


def cmd_generate_docs(_: argparse.Namespace) -> int:
    """Regenerate docs/MODULES.md from module manifests."""
    modules_dir = ROOT / "modules"
    docs_path = ROOT / "docs" / "MODULES.md"
    docs_path.parent.mkdir(parents=True, exist_ok=True)

    rows: list[dict] = []
    for child in sorted(modules_dir.iterdir()):
        manifest_path = child / "manifest.json"
        if not manifest_path.exists():
            continue
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["__dir__"] = child.name
        rows.append(manifest)

    lines = [
        "# GallusOS Modules",
        "",
        "Auto-generated by `python3 tools/gallus.py generate-docs`.",
        "Do not edit by hand.",
        "",
        "| Module | Version | Category | Description |",
        "|---|---|---|---|",
    ]
    for m in rows:
        lines.append(
            f"| `{m['name']}` | {m['version']} | {m['category']} | "
            f"{m['description']} |")

    lines.extend(["", "## Details", ""])
    for m in rows:
        lines.append(f"### `{m['name']}`")
        lines.append("")
        lines.append(f"- **Version:** {m['version']}")
        lines.append(f"- **Category:** {m['category']}")
        lines.append(f"- **Author:** {m['author']}")
        lines.append(f"- **License:** {m['license']}")
        svcs = m.get("required_services", [])
        if svcs:
            lines.append(f"- **Services:** {', '.join(svcs)}")
        readme = modules_dir / m["__dir__"] / "README.md"
        if readme.exists():
            lines.append("")
            lines.append(readme.read_text(encoding="utf-8").strip())
        lines.append("")

    docs_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {docs_path.relative_to(ROOT)}")
    return 0


def cmd_validate_all(_: argparse.Namespace) -> int:
    modules = ROOT / "modules"
    failed = False
    for child in sorted(modules.iterdir()):
        if not child.is_dir() or not (child / "manifest.json").exists():
            continue
        rc = cmd_validate_module(argparse.Namespace(path=str(child)))
        if rc != 0:
            failed = True
    return 1 if failed else 0


def cmd_host_sim(_: argparse.Namespace) -> int:
    """Build and run the desktop kernel harness."""
    host_dir = ROOT / "host"
    build_dir = host_dir / "build"
    binary = build_dir / "gallus_host_sim"

    for cmd in (
        ["cmake", "-S", str(host_dir), "-B", str(build_dir)],
        ["cmake", "--build", str(build_dir), "-j"],
    ):
        rc = subprocess.call(cmd, cwd=ROOT)
        if rc != 0:
            return rc

    return subprocess.call([str(binary)], cwd=ROOT)


def main() -> int:
    parser = argparse.ArgumentParser(prog="gallus",
                                     description="GallusOS SDK CLI")
    sub = parser.add_subparsers(dest="command", required=True)

    create = sub.add_parser("create-module", help="scaffold a new module")
    create.add_argument("name", help="lower_snake_case module name")
    create.add_argument("--description", help="short module description")
    create.add_argument("--category", help="manifest category")
    create.set_defaults(func=cmd_create_module)

    create_service = sub.add_parser("create-service",
                                    help="scaffold a new core service")
    create_service.add_argument("name", help="lower_snake_case service name")
    create_service.add_argument("--description", help="short service description")
    create_service.set_defaults(func=cmd_create_service)

    create_driver = sub.add_parser("create-driver",
                                   help="scaffold a new hardware driver")
    create_driver.add_argument("name", help="lower_snake_case driver name")
    create_driver.add_argument("--description", help="short driver description")
    create_driver.set_defaults(func=cmd_create_driver)

    validate = sub.add_parser("validate-module",
                              help="validate a module manifest")
    validate.add_argument("path", help="module directory or manifest.json")
    validate.set_defaults(func=cmd_validate_module)

    validate_all = sub.add_parser("validate-all",
                                  help="validate every module manifest")
    validate_all.set_defaults(func=cmd_validate_all)

    lint = sub.add_parser("lint", help="lint module structure and services")
    lint.add_argument("path", nargs="?", help="module directory (default: all)")
    lint.set_defaults(func=cmd_lint)

    gen_docs = sub.add_parser("generate-docs",
                              help="regenerate docs/MODULES.md from manifests")
    gen_docs.set_defaults(func=cmd_generate_docs)

    host_sim = sub.add_parser("host-sim",
                              help="build and run the desktop kernel harness")
    host_sim.set_defaults(func=cmd_host_sim)

    build = sub.add_parser("build", help="run idf.py build")
    build.set_defaults(func=lambda _: run_idf(["build"]))

    flash = sub.add_parser("flash", help="run idf.py flash")
    flash.add_argument("-p", "--port", default=os.environ.get("ESPPORT"))
    flash.set_defaults(func=lambda a: run_idf(
        ["-p", a.port, "flash"] if a.port else ["flash"]))

    monitor = sub.add_parser("monitor", help="run idf.py monitor")
    monitor.add_argument("-p", "--port", default=os.environ.get("ESPPORT"))
    monitor.set_defaults(func=lambda a: run_idf(
        ["-p", a.port, "monitor"] if a.port else ["monitor"]))

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
