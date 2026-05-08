#!/usr/bin/env python3
"""
读 3rdparty.json，从官方 git 仓库拉取并编译指定的依赖、安装到统一前
缀（默认 D:\\3rdparty）。脚本是 JSON 驱动的——3rdparty.json 是依赖
真相来源，本脚本只负责按它执行。

前置条件（Windows）:
  - PATH 中有 cmake 与 git
  - LunarG Vulkan SDK（仅当目标依赖需要 Vulkan 头时）

用法示例:
  python scripts/fetch_and_build_3rdparty.py --only EnTT
  python scripts/fetch_and_build_3rdparty.py --only EnTT --prefix E:\\deps
  python scripts/fetch_and_build_3rdparty.py --list

3rdparty.json 中每条依赖必须至少含 name / url；可选 version / cmake_extra。
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Sequence

# CI runners (e.g. GitHub Actions on Windows) default stdout/stderr to cp1252
# which can't encode the Chinese strings used below.
for _stream in (sys.stdout, sys.stderr):
    reconfigure = getattr(_stream, "reconfigure", None)
    if reconfigure is not None:
        reconfigure(encoding="utf-8", errors="replace")


REPO_ROOT = Path(__file__).resolve().parent.parent
MANIFEST = REPO_ROOT / "3rdparty.json"
DEFAULT_PREFIX = Path(r"D:\3rdparty")


@dataclass(frozen=True)
class DepSpec:
    name: str
    git_url: str
    tag: str | None
    cmake_extra: tuple[str, ...] = field(default_factory=tuple)
    optional: bool = False
    # 接入模式：
    #   "cmake_install"（默认）—— 仓根有 CMakeLists，cmake -S/-B + build + install
    #     到 <prefix>/install，消费者通过 find_package(<name>) 引用；
    #   "submodule" —— 已在 .gitmodules 中注册的 git submodule（如 DragonBonesCPP）；
    #     脚本走 `git submodule update --init <vendor_dest>` 同步，**不**跑 cmake。
    #     orange_engine 顶层 CMakeLists 自己 add_library 把 .cpp 编进去。
    #   "vendor_clone" —— 直接 clone 到 vendor_dest（仓库相对路径），不走 submodule
    #     机制、不跑 cmake；适合"我就要 vendored 但不想注册成 submodule"的边缘情况。
    mode: str = "cmake_install"
    vendor_dest: str | None = None  # mode == "submodule" / "vendor_clone" 时有效


def _read_manifest() -> list[DepSpec]:
    if not MANIFEST.is_file():
        raise FileNotFoundError(f"未找到清单文件: {MANIFEST}")
    data = json.loads(MANIFEST.read_text(encoding="utf-8"))
    deps: list[DepSpec] = []
    for entry in data.get("dependencies", []):
        name = entry.get("name")
        url  = entry.get("url")
        if not name or not url:
            continue
        deps.append(DepSpec(
            name=name,
            git_url=url,
            tag=entry.get("version"),
            cmake_extra=tuple(entry.get("cmake_extra", []) or []),
            optional=bool(entry.get("optional", False)),
            mode=entry.get("mode") or "cmake_install",
            vendor_dest=entry.get("vendor_dest"),
        ))
    return deps


def _run(cmd: Sequence[str], *, cwd: Path | None = None, env: dict[str, str] | None = None) -> None:
    printable = " ".join(cmd)
    print(f"[exec] {printable}", flush=True)
    subprocess.run(list(cmd), cwd=cwd, env=env, check=True)


def _which(tool: str) -> str:
    found = shutil.which(tool)
    if not found:
        raise RuntimeError(f"未在 PATH 中找到 {tool}，请先安装并加入 PATH。")
    return found


def _is_multi_config_generator(generator: str) -> bool:
    g = generator.lower()
    return "visual studio" in g or "xcode" in g


def _default_generator() -> str:
    if sys.platform == "win32":
        return "Visual Studio 17 2022"
    if sys.platform == "darwin":
        return "Xcode"
    return "Ninja"


def _ensure_clone(git: str, dest: Path, url: str, tag: str | None) -> None:
    # `.git` 既可能是 dir（普通 clone）也可能是 file（submodule 的 gitlink），
    # 用 .exists() 一并接受。
    if dest.exists() and (dest / ".git").exists():
        # 已有本地仓库 / 已注册为 submodule：fetch 然后切到目标 tag。
        _run([git, "-C", str(dest), "fetch", "origin", "--tags"])
        if tag:
            _run([git, "-C", str(dest), "checkout", tag])
        return
    dest.parent.mkdir(parents=True, exist_ok=True)
    if tag:
        _run([git, "clone", "--depth", "1", "--branch", tag, url, str(dest)])
    else:
        # 没指定 tag → 浅克隆默认分支。
        _run([git, "clone", "--depth", "1", url, str(dest)])


def _cmake_configure(
    cmake: str,
    *,
    source: Path,
    build: Path,
    install_prefix: Path,
    generator: str,
    dep_cmake_args: Sequence[str],
    config: str | None,
) -> None:
    build.mkdir(parents=True, exist_ok=True)
    cmd = [
        cmake,
        "-S", str(source),
        "-B", str(build),
        f"-DCMAKE_INSTALL_PREFIX={install_prefix}",
        f"-DCMAKE_PREFIX_PATH={install_prefix}",
        "-G", generator,
    ]
    if sys.platform == "win32" and "visual studio" in generator.lower():
        cmd += ["-A", "x64"]
    if not _is_multi_config_generator(generator):
        cmd.append(f"-DCMAKE_BUILD_TYPE={config or 'Release'}")
    cmd.extend(dep_cmake_args)
    _run(cmd)


def _cmake_build_install(
    cmake: str,
    build: Path,
    *,
    jobs: int,
    config: str,
    multi_config: bool,
) -> None:
    build_cmd   = [cmake, "--build", str(build), "--parallel", str(jobs)]
    install_cmd = [cmake, "--install", str(build)]
    if multi_config:
        build_cmd   += ["--config", config]
        install_cmd += ["--config", config]
    _run(build_cmd)
    _run(install_cmd)


def _build_dep(
    *,
    cmake: str,
    git: str,
    prefix: Path,
    dep: DepSpec,
    generator: str,
    jobs: int,
    config: str,
) -> None:
    label = dep.tag or "default-branch"
    print(f"\n=== 依赖: {dep.name} @ {label} (mode={dep.mode}) ===", flush=True)

    if dep.mode == "submodule":
        # 已注册为 git submodule（.gitmodules 中）。脚本走 update --init 同步。
        if not dep.vendor_dest:
            raise ValueError(f"{dep.name}: mode=submodule 但缺 vendor_dest 字段")
        dest = (REPO_ROOT / dep.vendor_dest).resolve()
        _run([git, "-C", str(REPO_ROOT), "submodule", "update", "--init", dep.vendor_dest])
        if dep.tag:
            # 用户指定了 tag / branch，切到该 ref
            _run([git, "-C", str(dest), "fetch", "origin", "--tags"])
            _run([git, "-C", str(dest), "checkout", dep.tag])
        print(f"[info] {dep.name} submodule 已就位 @ {dest}", flush=True)
        return

    if dep.mode == "vendor_clone":
        # in-tree 源码集成：clone 到仓库相对路径，**不跑 cmake**。
        # 由消费方（OrangeEngine 顶层 CMakeLists）把 .cpp 加进 add_library。
        if not dep.vendor_dest:
            raise ValueError(f"{dep.name}: mode=vendor_clone 但缺 vendor_dest 字段")
        dest = (REPO_ROOT / dep.vendor_dest).resolve()
        _ensure_clone(git, dest, dep.git_url, dep.tag)
        print(f"[info] {dep.name} 已落到 {dest}（in-tree，不跑 cmake）", flush=True)
        return

    if dep.mode != "cmake_install":
        raise ValueError(
            f"{dep.name}: 未知 mode '{dep.mode}'，"
            "请用 'cmake_install' / 'submodule' / 'vendor_clone'"
        )

    src = prefix / "src" / dep.name
    bld = prefix / "build" / dep.name
    install_root = prefix / "install"
    _ensure_clone(git, src, dep.git_url, dep.tag)
    multi = _is_multi_config_generator(generator)
    _cmake_configure(
        cmake,
        source=src,
        build=bld,
        install_prefix=install_root,
        generator=generator,
        dep_cmake_args=list(dep.cmake_extra),
        config=config if not multi else None,
    )
    _cmake_build_install(cmake, bld, jobs=jobs, config=config, multi_config=multi)


def _parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(description="JSON 驱动的第三方依赖拉取与安装。")
    p.add_argument("--prefix",  type=Path, default=DEFAULT_PREFIX,
                   help=f"第三方根目录（默认: {DEFAULT_PREFIX}），源码在 <prefix>/src，安装在 <prefix>/install")
    p.add_argument("--jobs", "-j", type=int, default=max(1, (os.cpu_count() or 4)),
                   help="并行编译线程数")
    p.add_argument("--config", choices=("Release", "Debug", "RelWithDebInfo", "MinSizeRel"),
                   default="Release", help="构建配置（多配置生成器下用作 --config）")
    p.add_argument("--generator", "-G", default=_default_generator(),
                   help="CMake 生成器")
    p.add_argument("--only", action="append", default=None,
                   help="只构建指定的依赖名（可重复指定）。不给则按 manifest 全量构建。")
    p.add_argument("--list", action="store_true",
                   help="只列出 manifest 中的依赖名，不做任何操作。")
    return p.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(argv if argv is not None else sys.argv[1:])
    deps = _read_manifest()

    if args.list:
        for d in deps:
            tag = d.tag or "(no version pinned)"
            mode = d.mode if d.mode != "cmake_install" else ""
            mode_col = f"[{mode}]" if mode else ""
            print(f"{d.name:<20} {tag:<14} {mode_col:<18} {d.git_url}")
        return 0

    selected: list[DepSpec]
    if args.only:
        wanted = {n for n in args.only}
        known = {d.name for d in deps}
        unknown = wanted - known
        if unknown:
            print(f"[error] manifest 中没有这些依赖: {', '.join(sorted(unknown))}", flush=True)
            return 2
        selected = [d for d in deps if d.name in wanted]
    else:
        # 不指定 --only 时只跑非 optional 的；optional 必须显式 opt-in。
        selected = [d for d in deps if not d.optional]

    if not selected:
        print("[info] 没有要构建的依赖。", flush=True)
        return 0

    cmake = _which("cmake")
    git   = _which("git")

    for dep in selected:
        if dep.tag is None and dep.mode != "submodule":
            # submodule 由父仓 .gitmodules + 父仓提交里的 gitlink 自动 pin commit；
            # 缺 version 仅对 cmake_install / vendor_clone 模式是真问题。
            print(f"[warn] {dep.name} 在 manifest 中未指定 version，将拉取默认分支；建议在 3rdparty.json 中加 version 字段。",
                  flush=True)
        _build_dep(
            cmake=cmake,
            git=git,
            prefix=args.prefix,
            dep=dep,
            generator=args.generator,
            jobs=args.jobs,
            config=args.config,
        )

    print(f"\n[done] 已构建并安装 {len(selected)} 个依赖到 {args.prefix / 'install'}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
