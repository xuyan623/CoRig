#!/usr/bin/env python3
"""Offline closure collector for CoRig Python plugins.

The collector runs with the *developer* interpreter that owns the packages a
plugin declares. It never downloads anything: every distribution must already be
installed for that interpreter. The output is a self-contained runtime tree:

    <payload>/python/python.exe
    <payload>/python/pythonXY.dll
    <payload>/python/DLLs/...
    <payload>/python/Lib/...                (standard library)
    <payload>/python/Lib/site-packages/     (the resolved closure)
    <payload>/python/worker.py              (written by the caller beforehand)
    <payload>/python/python-runtime.json    (manifest of the closure)

The caller writes worker.py before collection because the publishing binary owns
the worker script version; this program only verifies that it exists.

Request (stdin or --request FILE):

    {
      "payload_root": "D:/build/staging",
      "plugins": [
        {"package": "pkg.id", "id": "node.id",
         "python": {"distributions": {"模块名": "分发名"},
                    "requires": {"分发名": ">=1.2"},
                    "extras": {"分发名": ["extra"]}}}
      ]
    }

Result (stdout, one JSON object; diagnostics go to stderr):

    {"ok": true, "python": "3.15.1", "site_packages": "...", "files": 123,
     "distributions": {"paramiko": {"version": "5.1.0", "files": 49,
                                    "plugins": ["node.id"]}},
     "probe": {...}, "warnings": [...]}

Codes reported in "code" on failure:

    PY_PACKAGING_MISSING  the interpreter cannot import packaging
    PY_DECL_INVALID       a python block is malformed
    PY_DECL_UNRESOLVED    an import name or dependency is not installed
    PY_CONFLICT           installed versions do not satisfy the declaration
    PY_RECORD_MISSING     a distribution exposes no files to copy
    PY_DEV_PATH           collected files still reference the developer machine
    PY_PROBE_FAILED       the payload interpreter failed self verification
    PY_WORKER_MISSING     worker.py was not placed in the payload first
    PY_LAYOUT             the destination layout is not usable
"""

from __future__ import annotations

import argparse
import importlib.metadata as metadata
import json
import re
import shutil
import subprocess
import sys
from collections import defaultdict, deque
from pathlib import Path, PurePosixPath

FORMAT = "corig-python-runtime"
FORMAT_VERSION = 1
MANIFEST = "python-runtime.json"

# Standard library parts a headless worker never needs. site-packages is rebuilt
# from the closure so the developer's pip/setuptools never leak into a payload.
EXCLUDED_LIB_DIRS = frozenset({
    "test", "idlelib", "tkinter", "turtledemo", "site-packages", "__pycache__",
})
EXCLUDED_DLL_FILES = frozenset({"_tkinter.pyd"})
EXCLUDED_DLL_PREFIXES = ("_test", "tcl", "tk")

ROOT_FILES = ("python.exe", "pythonw.exe", "LICENSE.txt")
ROOT_PREFIXES = ("python", "vcruntime")

MODULE_NAME = re.compile(r"^[A-Za-z_][A-Za-z0-9_.-]*$")
DISTRIBUTION_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")

# Executed with the payload interpreter. It reports paths only; the caller
# asserts every one of them stays inside the payload. Kept in sync with the
# probe in src/core/python_bundle.cpp.
PROBE_SOURCE = r"""
import json, os, sys

payload = os.path.normcase(os.path.abspath(sys.argv[1]))
def inside(path):
    if not path:
        return None
    candidate = os.path.normcase(os.path.abspath(path))
    try:
        return os.path.commonpath([candidate, payload]) == payload
    except ValueError:
        return False

modules = json.loads(sys.argv[2])
info = {
    "executable": sys.executable,
    "prefix": sys.prefix,
    "base_prefix": sys.base_prefix,
    "version": "%d.%d.%d" % sys.version_info[:3],
    "site_packages": None,
    "path": list(sys.path),
    "modules": {},
}
for entry in sys.path:
    if entry and os.path.basename(entry).lower() == "site-packages":
        info["site_packages"] = entry
        break
for name in modules:
    record = {"ok": False}
    try:
        module = __import__(name)
        record["ok"] = True
        record["file"] = getattr(module, "__file__", None)
        if record["file"] is None:
            record["path"] = [str(item) for item in getattr(module, "__path__", []) or []]
        version = None
        try:
            from importlib.metadata import version as distribution_version
            parts = name.split(".")
            for length in range(len(parts), 0, -1):
                try:
                    version = distribution_version(".".join(parts[:length]))
                    break
                except Exception:
                    continue
        except Exception:
            version = None
        record["version"] = version
    except Exception as error:
        record["error"] = "%s: %s" % (type(error).__name__, error)
    info["modules"][name] = record
info["violations"] = sorted({
    key for key in ("executable", "prefix", "base_prefix", "site_packages")
    if inside(info[key]) is False
})
print(json.dumps(info, ensure_ascii=False))
"""


class CollectorError(Exception):
    def __init__(self, code: str, detail: str, **extra):
        super().__init__(detail)
        self.code = code
        self.detail = detail
        self.extra = extra

    def document(self) -> dict:
        document = {"ok": False, "code": self.code, "detail": self.detail}
        document.update(self.extra)
        return document


def packaging_tools():
    try:
        from packaging.requirements import Requirement
        from packaging.specifiers import SpecifierSet
        from packaging.utils import canonicalize_name
    except ImportError as error:  # pragma: no cover - environment problem
        raise CollectorError(
            "PY_PACKAGING_MISSING",
            "用于收集的解释器缺少 packaging，无法离线求值依赖约束：" + str(error),
        )
    return Requirement, SpecifierSet, canonicalize_name


def require_string(value, scope: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise CollectorError("PY_DECL_INVALID", scope + " 需要非空字符串")
    return value.strip()


def python_block(block, plugin: str, warnings: list) -> dict:
    """Validates one plugin's python block and normalises it."""
    if block is None:
        block = {}
    if not isinstance(block, dict):
        raise CollectorError("PY_DECL_INVALID", "%s 的 python 需要对象" % plugin)
    for key in sorted(set(block) - {"distributions", "requires", "extras"}):
        warnings.append("%s: 忽略未知的 python.%s 声明" % (plugin, key))

    distributions = {}
    raw = block.get("distributions") or {}
    if not isinstance(raw, dict):
        raise CollectorError("PY_DECL_INVALID", "%s 的 python.distributions 需要对象" % plugin)
    for module, name in raw.items():
        if not MODULE_NAME.match(str(module)):
            raise CollectorError("PY_DECL_INVALID", "%s 的导入名不合法：%s" % (plugin, module))
        distribution = require_string(name, "%s 的 python.distributions.%s" % (plugin, module))
        if not DISTRIBUTION_NAME.match(distribution):
            raise CollectorError("PY_DECL_INVALID", "%s 的分发名不合法：%s" % (plugin, distribution))
        distributions[str(module)] = distribution

    requires = {}
    raw = block.get("requires") or {}
    if not isinstance(raw, dict):
        raise CollectorError("PY_DECL_INVALID", "%s 的 python.requires 需要对象" % plugin)
    for name, specifier in raw.items():
        if not DISTRIBUTION_NAME.match(str(name)):
            raise CollectorError("PY_DECL_INVALID", "%s 的依赖名不合法：%s" % (plugin, name))
        if isinstance(specifier, (int, float)):
            specifier = "==" + str(specifier)
        if not isinstance(specifier, str):
            raise CollectorError("PY_DECL_INVALID", "%s 的 python.requires.%s 需要版本约束字符串" % (plugin, name))
        requires[str(name)] = specifier.strip()

    extras = {}
    raw = block.get("extras") or {}
    if not isinstance(raw, dict):
        raise CollectorError("PY_DECL_INVALID", "%s 的 python.extras 需要对象" % plugin)
    for name, values in raw.items():
        if not DISTRIBUTION_NAME.match(str(name)):
            raise CollectorError("PY_DECL_INVALID", "%s 的附加项名不合法：%s" % (plugin, name))
        if isinstance(values, str):
            values = [values]
        if not isinstance(values, list) or not all(isinstance(item, str) for item in values):
            raise CollectorError("PY_DECL_INVALID", "%s 的 python.extras.%s 需要字符串列表" % (plugin, name))
        extras[str(name)] = [item.strip() for item in values if item.strip()]

    return {"distributions": distributions, "requires": requires, "extras": extras}


def load_request(arguments) -> dict:
    if arguments.request:
        request = json.loads(Path(arguments.request).read_text(encoding="utf-8"))
    else:
        request = json.loads(sys.stdin.read() or "{}")
    if not isinstance(request, dict):
        raise CollectorError("PY_LAYOUT", "请求需要 JSON 对象")
    if arguments.payload_root:
        request["payload_root"] = arguments.payload_root
    if arguments.worker_source:
        request["worker_source"] = arguments.worker_source
    return request


def declared_plugins(request: dict, warnings: list) -> list:
    plugins = request.get("plugins") or []
    if not isinstance(plugins, list):
        raise CollectorError("PY_DECL_INVALID", "plugins 需要列表")
    result = []
    for entry in plugins:
        if not isinstance(entry, dict):
            raise CollectorError("PY_DECL_INVALID", "plugins 的条目需要对象")
        identifier = require_string(entry.get("id") or entry.get("plugin") or "plugin", "插件声明")
        package = str(entry.get("package") or "")
        block = python_block(entry.get("python"), identifier, warnings)
        result.append({"id": identifier, "package": package, **block})
    return result


def search_locations() -> str:
    """Names where this interpreter looked, so PY_DECL_UNRESOLVED is actionable."""
    packages = []
    for entry in sys.path:
        name = Path(entry).name.lower()
        if name in ("site-packages", "dist-packages") and entry not in packages:
            packages.append(entry)
    if packages:
        detail = "已搜索：解释器 %s；site-packages %s" % (sys.executable, "、".join(packages))
    else:
        detail = "已搜索：解释器 %s；sys.path %s" % (
            sys.executable, "；".join(str(entry) for entry in sys.path))
    return "。" + detail


def resolve_modules(plugins: list, packages_distributions: dict, canonicalize) -> tuple[dict, dict]:
    """Maps declared import names to canonical distributions.

    The explicit declarations are authoritative; packages_distributions() only
    fills in the import name of a distribution the plugin already named.
    """
    modules = {}
    unresolved = []
    for plugin in plugins:
        for module, explicit in plugin["distributions"].items():
            candidates = [explicit] if explicit else list(packages_distributions.get(module, ()))
            if not candidates:
                unresolved.append({"plugin": plugin["id"], "module": module})
                continue
            name = candidates[0]
            try:
                metadata.distribution(name)
            except metadata.PackageNotFoundError:
                unresolved.append({"plugin": plugin["id"], "module": module})
                continue
            modules[module] = {"distribution": name, "canonical": canonicalize(name),
                               "plugin": plugin["id"]}
    if unresolved:
        raise CollectorError(
            "PY_DECL_UNRESOLVED",
            "以下导入名无法在采集用解释器中解析：" +
            "、".join("%s（插件 %s）" % (item["module"], item["plugin"]) for item in unresolved) +
            search_locations(),
            unresolved=unresolved,
        )
    return modules, {item["canonical"] for item in modules.values()}


def resolve_closure(declared, module_distributions, module_plugins, plugin_requirements,
                    canonicalize, Requirement):
    """Breadth-first Requires-Dist closure with markers and extras.

    Every environment marker is evaluated with the collecting interpreter, which
    is the same CPython build that becomes the payload. Nothing is downloaded.
    """
    constraints = defaultdict(list)
    for distribution, entries in plugin_requirements.items():
        for plugin, specifier in entries:
            constraints[canonicalize(distribution)].append((plugin, specifier))

    state = {}

    def entry_for(name, parent=None, plugin=None):
        entry = state.get(name)
        if entry is None:
            try:
                distribution = metadata.distribution(name)
            except metadata.PackageNotFoundError:
                raise CollectorError(
                    "PY_DECL_UNRESOLVED",
                    "依赖 %s 未安装（由 %s 需要）" % (name, parent or "插件声明") +
                    search_locations(),
                )
            entry = state[name] = {
                "distribution": distribution, "extras": set(),
                "plugins": set(), "required_by": set(),
            }
        if parent:
            entry["required_by"].add(parent)
        if plugin:
            entry["plugins"].add(plugin)
        return entry

    pending = []
    for name in sorted(set(plugin_requirements) | set(module_distributions)):
        entry_for(canonicalize(name), None)
        pending.append(canonicalize(name))
    for plugin in declared:
        for name, extras in plugin["extras"].items():
            entry = entry_for(canonicalize(name), plugin=plugin["id"])
            entry["extras"].update(extras)
            pending.append(canonicalize(name))

    # Direct declarations decide which plugin a distribution is attributed to;
    # the closure only records which distribution pulled in each dependency.
    direct = defaultdict(set)
    for name, entries in module_plugins.items():
        direct[name] |= set(entries)
    for plugin in declared:
        for name in list(plugin["requires"]) + list(plugin["extras"]):
            direct[canonicalize(name)].add(plugin["id"])

    processed = set()
    while pending:
        name = pending.pop(0)
        entry = entry_for(name, None)
        # The base group and every declared extra are evaluated independently;
        # an extra that pulls a new dependency enqueues it for the next pass.
        for extra in [""] + sorted(entry["extras"] - {""}):
            if (name, extra) in processed:
                continue
            processed.add((name, extra))
            for raw in entry["distribution"].requires or []:
                requirement = Requirement(raw)
                if requirement.marker is not None:
                    try:
                        if not requirement.marker.evaluate({"extra": extra}):
                            continue
                    except Exception as error:  # pragma: no cover - packaging detail
                        raise CollectorError("PY_DECL_INVALID",
                                             "无法求值 %s 的环境标记：%s" % (raw, error))
                dependency = canonicalize(requirement.name)
                constraints[dependency].append((name, str(requirement.specifier)))
                target = entry_for(dependency, parent=name)
                target["extras"].update(requirement.extras)
                pending.append(dependency)

    conflicts = []
    for name, entries in constraints.items():
        entry = state.get(name)
        if entry is None:
            continue
        installed = entry["distribution"].version
        from packaging.specifiers import SpecifierSet  # local: only needed here
        failed = []
        seen = set()
        for source, specifier in entries:
            if not specifier or (source, specifier) in seen:
                continue
            seen.add((source, specifier))
            try:
                satisfied = SpecifierSet(specifier).contains(installed, prereleases=True)
            except Exception as error:
                raise CollectorError("PY_DECL_INVALID",
                                     "%s 的版本约束 %s 无法求值：%s" % (name, specifier, error))
            if not satisfied:
                failed.append({"source": source, "specifier": specifier})
        if failed:
            conflicts.append({
                "distribution": name,
                "installed": installed,
                "requirements": [{"source": source, "specifier": specifier}
                                 for source, specifier in entries if specifier],
            })
    if conflicts:
        raise CollectorError(
            "PY_CONFLICT",
            "依赖版本冲突：" + "；".join(
                "%s 已安装 %s，不满足 %s" % (
                    item["distribution"], item["installed"],
                    "、".join("%s %s" % (requirement["source"], requirement["specifier"])
                             for requirement in item["requirements"]))
                for item in conflicts),
            conflicts=conflicts,
        )
    return state, direct


def excluded_dll(name: str) -> bool:
    return name in EXCLUDED_DLL_FILES or name.startswith(EXCLUDED_DLL_PREFIXES)


def copy_runtime_tree(source: Path, destination: Path) -> None:
    """Copies the standard library and DLLs without developer-only parts."""
    shutil.copytree(
        source, destination,
        ignore=lambda directory, names: {
            name for name in names
            if (Path(directory) / name).is_dir() and name in EXCLUDED_LIB_DIRS
        },
    )


def copy_base_runtime(base: Path, python_dir: Path, warnings: list) -> None:
    python_dir.mkdir(parents=True, exist_ok=True)
    for name in ROOT_FILES:
        source = base / name
        if source.is_file():
            shutil.copyfile(source, python_dir / name)
    for source in sorted(base.iterdir()):
        if not source.is_file() or source.name in ROOT_FILES:
            continue
        if source.suffix.lower() in {".exe", ".dll"} and source.name.lower().startswith(ROOT_PREFIXES):
            shutil.copyfile(source, python_dir / source.name)
    dlls = base / "DLLs"
    if dlls.is_dir():
        (python_dir / "DLLs").mkdir(exist_ok=True)
        skipped = []
        for source in sorted(dlls.iterdir()):
            if not source.is_file():
                continue
            if excluded_dll(source.name.lower()):
                skipped.append(source.name)
                continue
            shutil.copyfile(source, python_dir / "DLLs" / source.name)
        if skipped:
            warnings.append("已排除不参与无界面运行的 DLL：" + "、".join(sorted(skipped)))
    library = base / "Lib"
    if not library.is_dir():
        raise CollectorError("PY_LAYOUT", "解释器缺少标准库目录：" + str(library))
    copy_runtime_tree(library, python_dir / "Lib")
    (python_dir / "Lib" / "site-packages").mkdir(parents=True, exist_ok=True)


def copy_distribution(distribution, destination: Path, warnings: list) -> tuple[int, int]:
    """Copies one installed distribution from its RECORD and returns counts."""
    root = Path(distribution.locate_file("")).resolve()
    files = list(distribution.files or [])
    if not files:
        return copy_top_level(distribution, root, destination, warnings)
    copied = 0
    bytes_copied = 0
    skipped = 0
    for record in files:
        relative = PurePosixPath(str(record).replace("\\", "/"))
        if relative.is_absolute():
            raise CollectorError("PY_DEV_PATH",
                                 "%s 的记录包含绝对路径：%s" % (distribution.metadata["Name"], record))
        if ".." in relative.parts:
            skipped += 1
            continue
        if "__pycache__" in relative.parts or relative.suffix.lower() in {".pyc", ".pyo"}:
            continue
        source = Path(distribution.locate_file(record))
        if not source.is_file():
            skipped += 1
            continue
        target = destination.joinpath(*relative.parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        copied += 1
        bytes_copied += target.stat().st_size
    if skipped:
        warnings.append("%s: 跳过 %d 个包外记录（脚本或缺失文件）" % (
            distribution.metadata["Name"], skipped))
    return copied, bytes_copied


def copy_top_level(distribution, root: Path, destination: Path, warnings: list) -> tuple[int, int]:
    """Fallback for distributions without RECORD: copy named top-level entries."""
    name = distribution.metadata["Name"]
    declared = (distribution.read_text("top_level.txt") or "").split()
    if not declared:
        raise CollectorError("PY_RECORD_MISSING",
                             "%s 没有 RECORD 也没有 top_level.txt，无法确定要复制的文件" % name)
    copied = 0
    bytes_copied = 0
    for item in declared:
        source = root / item
        if source.is_dir():
            target = destination / item
            shutil.copytree(source, target, dirs_exist_ok=True,
                            ignore=lambda directory, names: {"__pycache__"} & set(names))
            copied += sum(1 for path in target.rglob("*") if path.is_file())
        elif source.is_file():
            target = destination / item
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
            copied += 1
        else:
            continue
        bytes_copied = sum(path.stat().st_size for path in destination.rglob("*") if path.is_file())
    for candidate in root.glob("*.dist-info"):
        if candidate.name.lower().startswith(name.replace("-", "_").lower()):
            shutil.copytree(candidate, destination / candidate.name, dirs_exist_ok=True)
    warnings.append("%s 没有 RECORD，已按 top_level.txt 复制" % name)
    return copied, bytes_copied


def scan_developer_paths(python_dir: Path, prefixes) -> list:
    """Finds absolute references that would tie the payload to this machine."""
    hits = []
    suffixes = {".py", ".pth", ".cfg", ".json", ".txt", ".ini"}
    for path in python_dir.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in suffixes:
            continue
        try:
            data = path.read_bytes()
        except OSError:
            continue
        if len(data) > 4 * 1024 * 1024:
            continue
        for prefix in prefixes:
            if prefix and prefix.encode("utf-8", "ignore") in data:
                hits.append({"file": str(path.relative_to(python_dir)), "prefix": prefix})
                break
    return hits


def probe(python_dir: Path, modules, payload_root: Path) -> dict:
    executable = python_dir / "python.exe"
    process = subprocess.run(
        [str(executable), "-I", "-B", "-X", "utf8", "-c", PROBE_SOURCE,
         str(python_dir), json.dumps(sorted(modules))],
        cwd=str(python_dir), capture_output=True, text=True, timeout=300,
    )
    if process.returncode != 0:
        raise CollectorError("PY_PROBE_FAILED",
                             "载荷解释器自检失败：" + (process.stderr or "").strip()[-2000:])
    try:
        info = json.loads(process.stdout.strip().splitlines()[-1])
    except (ValueError, IndexError) as error:
        raise CollectorError("PY_PROBE_FAILED",
                             "载荷解释器自检输出无法解析：%s；%s" % (error, process.stderr[-2000:]))
    violations = list(info.get("violations") or [])
    for name, record in (info.get("modules") or {}).items():
        if not record.get("ok"):
            violations.append("模块 %s 无法导入：%s" % (name, record.get("error")))
            continue
        if record.get("file"):
            try:
                Path(record["file"]).resolve().relative_to(payload_root.resolve())
            except ValueError:
                violations.append("模块 %s 来自载荷之外：%s" % (name, record["file"]))
        else:
            for entry in record.get("path") or []:
                try:
                    Path(entry).resolve().relative_to(payload_root.resolve())
                except ValueError:
                    violations.append("模块 %s 的搜索路径在载荷之外：%s" % (name, entry))
    if not info.get("site_packages"):
        violations.append("载荷没有 site-packages")
    if violations:
        raise CollectorError("PY_PROBE_FAILED", "载荷自检未通过：" + "；".join(violations),
                             probe=info, violations=violations)
    return info


def prepare_payload(python_dir: Path) -> bytes | None:
    """Makes room for a fresh runtime and keeps a caller-provided worker.py.

    Re-collecting over an existing payload replaces the runtime; the worker
    script belongs to the publishing binary, so the bytes seen before the reset
    are restored when no other source provides them.
    """
    if not python_dir.exists():
        return None
    entries = [entry for entry in python_dir.iterdir()]
    if not entries:
        return None
    worker = python_dir / "worker.py"
    preserved = worker.read_bytes() if worker.is_file() else None
    # A stale runtime from a previous collection is replaced; anything else is a
    # caller mistake that must not be deleted silently.
    if (python_dir / "python.exe").is_file() or (python_dir / MANIFEST).is_file():
        shutil.rmtree(python_dir)
        return preserved
    if [entry.name for entry in entries] != ["worker.py"]:
        raise CollectorError("PY_LAYOUT",
                             "目标 python 目录包含无法识别的文件：" +
                             "、".join(sorted(entry.name for entry in entries)))
    return preserved


def collect(request: dict) -> dict:
    warnings = list(request.get("warnings") or [])
    payload = Path(require_string(request.get("payload_root"), "payload_root"))
    payload = payload.resolve()
    python_dir = payload / "python"
    preserved_worker = prepare_payload(python_dir)

    plugins = declared_plugins(request, warnings)
    modules, module_distributions = resolve_modules(
        plugins, metadata.packages_distributions(), packaging_tools()[2])
    module_plugins = defaultdict(set)
    for record in modules.values():
        module_plugins[record["canonical"]].add(record["plugin"])

    plugin_requirements = defaultdict(list)
    for plugin in plugins:
        for name, specifier in plugin["requires"].items():
            plugin_requirements[name].append((plugin["id"], specifier))

    canonicalize = packaging_tools()[2]
    Requirement = packaging_tools()[0]
    state, direct = resolve_closure(plugins, module_distributions, module_plugins,
                                    plugin_requirements, canonicalize, Requirement)

    python_version = "%d.%d.%d" % sys.version_info[:3]
    base = Path(sys.base_prefix)
    if not (base / "Lib").is_dir():
        raise CollectorError("PY_LAYOUT", "解释器缺少 sys.base_prefix/Lib：" + str(base))
    payload_version = subprocess.run([str(base / "python.exe"), "-I", "-B", "-c",
                                      "import sys;print('%d.%d.%d' % sys.version_info[:3])"],
                                     capture_output=True, text=True, timeout=60)
    if payload_version.returncode != 0 or payload_version.stdout.strip() != python_version:
        warnings.append("采集解释器 %s 与基础运行时 %s 版本不一致，请用同一解释器采集" % (
            python_version, (payload_version.stdout or "").strip() or "未知"))

    copy_base_runtime(base, python_dir, warnings)

    site = python_dir / "Lib" / "site-packages"
    distributions = {}
    total_files = 0
    total_bytes = 0
    for name, entry in sorted(state.items()):
        count, size = copy_distribution(entry["distribution"], site, warnings)
        plugins_using = sorted(direct.get(name, set()) | entry.get("plugins", set()))
        distributions[name] = {
            "version": entry["distribution"].version,
            "files": count,
            "plugins": plugins_using,
        }
        total_files += count
        total_bytes += size
        if count == 0:
            warnings.append("%s 没有复制到任何文件" % name)

    worker = python_dir / "worker.py"
    if not worker.is_file() and preserved_worker is not None:
        worker.write_bytes(preserved_worker)
    if not worker.is_file():
        source = request.get("worker_source")
        if source and Path(source).is_file():
            shutil.copyfile(Path(source), worker)
    if not worker.is_file():
        raise CollectorError("PY_WORKER_MISSING",
                             "载荷缺少 worker.py；调用方必须先写入工作进程脚本：" + str(worker))

    manifest = {
        "format": FORMAT,
        "format_version": FORMAT_VERSION,
        "python": python_version,
        "implementation": "CPython",
        "executable": "python.exe",
        "worker": "worker.py",
        "site_packages": "Lib/site-packages",
        "modules": {module: record["distribution"] for module, record in sorted(modules.items())},
        "distributions": distributions,
    }
    (python_dir / MANIFEST).write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8")

    prefixes = {str(Path(sys.executable).parent), str(sys.prefix), str(sys.base_prefix)}
    leaks = scan_developer_paths(python_dir, sorted(prefixes))
    if leaks:
        raise CollectorError(
            "PY_DEV_PATH",
            "载荷仍包含开发机绝对路径：" + "、".join(
                "%s（%s）" % (item["file"], item["prefix"]) for item in leaks[:8]),
            paths=leaks,
        )

    info = probe(python_dir, set(manifest["modules"]), payload)

    payload_files = sum(1 for path in python_dir.rglob("*") if path.is_file())
    payload_bytes = sum(path.stat().st_size for path in python_dir.rglob("*") if path.is_file())
    return {
        "ok": True,
        "python": python_version,
        "payload": str(python_dir),
        "site_packages": str(site),
        "files": payload_files,
        "bytes": payload_bytes,
        "distributions": distributions,
        "modules": {name: {"version": record.get("version"), "file": record.get("file")}
                    for name, record in (info.get("modules") or {}).items()},
        "probe": info,
        "warnings": warnings,
    }


def selftest(base: Path, worker_source) -> dict:
    """Runs the three verification cases used by W2 without touching the network."""
    base.mkdir(parents=True, exist_ok=True)
    cases = []
    failures = {}

    def run(name, request):
        try:
            result = collect({**request, "payload_root": str(base / name),
                              "worker_source": worker_source})
            cases.append({"case": name, "ok": True, "files": result["files"],
                          "distributions": result["distributions"]})
            return result, None
        except CollectorError as error:
            cases.append({"case": name, "ok": False, "code": error.code,
                          "detail": error.detail, **error.extra})
            return None, error

    real, error = run("real", {"plugins": [{
        "id": "selftest.paramiko", "package": "selftest",
        "python": {"distributions": {"paramiko": "paramiko"},
                   "requires": {"paramiko": ">=5.0"},
                   "extras": {}}}]})
    if error:
        failures["real"] = error.detail

    stdlib, error = run("stdlib", {"plugins": [{"id": "selftest.stdlib",
                                                "package": "selftest", "python": {}}]})
    if error:
        failures["stdlib"] = error.detail

    conflict, error = run("conflict", {"plugins": [
        {"id": "selftest.a", "package": "selftest", "python": {"requires": {"paramiko": ">=6"}}},
        {"id": "selftest.b", "package": "selftest", "python": {"requires": {"paramiko": "<4"}}},
    ]})
    if conflict is not None:
        failures["conflict"] = "互斥约束未被拒绝"
    elif error.code != "PY_CONFLICT":
        failures["conflict"] = "需要 PY_CONFLICT，实际为 %s" % error.code

    checks = {
        "paramiko_collected": bool(real and real["distributions"].get("paramiko", {}).get("files", 0) > 20),
        "paramiko_version": bool(real and real["distributions"].get("paramiko", {}).get("version")),
        "paramiko_import": bool(real and real["modules"].get("paramiko", {}).get("file")),
        "stdlib_runtime": bool(stdlib and stdlib["files"] > 300 and not stdlib["distributions"]),
        "conflict_reported": "conflict" not in failures,
        "no_developer_paths": bool(real and not scan_developer_paths(
            Path(real["payload"]),
            sorted({str(Path(sys.executable).parent), str(sys.prefix), str(sys.base_prefix)}))),
    }
    return {"ok": all(checks.values()) and not failures,
            "cases": cases, "checks": checks, "failures": failures}


def main() -> int:
    parser = argparse.ArgumentParser(description="CoRig offline Python closure collector")
    parser.add_argument("--request", help="JSON request file (default: stdin)")
    parser.add_argument("--payload-root", help="payload root; runtime goes to <root>/python")
    parser.add_argument("--worker-source", help="worker.py to copy when the payload lacks one")
    parser.add_argument("--selftest", help="run the verification cases under this directory")
    arguments = parser.parse_args()
    try:
        if arguments.selftest:
            result = selftest(Path(arguments.selftest), arguments.worker_source)
        else:
            result = collect(load_request(arguments))
    except CollectorError as error:
        print(json.dumps(error.document(), ensure_ascii=False))
        print(error.code + ": " + error.detail, file=sys.stderr)
        return 1
    except Exception as error:  # unexpected collector failure
        document = {"ok": False, "code": "PY_COLLECT_FAILED",
                    "detail": "%s: %s" % (type(error).__name__, error)}
        print(json.dumps(document, ensure_ascii=False))
        print(document["detail"], file=sys.stderr)
        return 1
    print(json.dumps(result, ensure_ascii=False))
    return 0 if result.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
