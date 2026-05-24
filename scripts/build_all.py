#!/usr/bin/env python3
"""
OrangeEngine 一键编译入口（ADR-009 sibling 拓扑版本）。

把"从 0 到 OrangeEditor 跑起来"所需的全部步骤串成单脚本：

  1. 同步本仓 git submodule（仅 vendor/DragonBones —— ADR-009 后 OrangeRender /
     Orange-Wiki 改走 sibling 拓扑，由 Orange-Ecosystem umbrella 仓持有，
     不再是本仓 submodule；本步对它们做的是"sibling 存在性 sanity check + Wiki
     分支校验"，不再 git submodule update）。
  2. 拉 + 编 OrangeRender 自己的第三方依赖（Vulkan stack / glfw / glm / volk / VMA …）
     到 `<prefix>/install`。
  3. 拉 + 编 OrangeEngine 直消费的第三方依赖（EnTT / nlohmann_json / box2d / miniaudio
     / stb / imgui / 可选 spdlog）到同一个 `<prefix>/install`。
  4. 编 + install OrangeRender 到 `<render-sdk>`（默认 D:\\sdk\\orange-render），让
     OrangeEngine 的 `find_package(OrangeRender CONFIG REQUIRED)` 能解析。
  5. 配置并编译 OrangeEngine（含 tools/OrangeEditor；samples 与 tests 默认关闭，
     用 `--with-samples` / `--with-tests` 显式开启），CMAKE_PREFIX_PATH 同时指向
     `<prefix>/install` 与 `<render-sdk>`。

前置条件（Windows）:
  - PATH 中有 cmake 与 git
  - 装好 LunarG Vulkan SDK，并设置环境变量 VULKAN_SDK
  - MSVC 2022（默认生成器 "Visual Studio 17 2022"）
  - **ADR-009 拓扑前置**：OrangeRender / Orange-Wiki 必须作为本仓 sibling 目录存在
    （典型布局 `<repo>/../OrangeRender/` + `<repo>/../Orange-Wiki/`，最常见的来源是
    `git clone --recursive https://github.com/JZNMCO55/Orange-Ecosystem.git`）。
    单独 clone 本仓不带 sibling 时本脚本会在 Step 1 就报错。

用法示例:
  python scripts/build_all.py
  python scripts/build_all.py --prefix E:\\deps --render-sdk E:\\sdk\\orange-render
  python scripts/build_all.py --config Release --jobs 16
  python scripts/build_all.py --skip-3rdparty --skip-render          # 仅重编引擎
  python scripts/build_all.py --with-tests --run-tests --with-spdlog
  python scripts/build_all.py --clean                                # 清掉所有构建目录后从头来
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Sequence

# Windows CI / 中文 console 兼容
for _stream in (sys.stdout, sys.stderr):
    reconfigure = getattr(_stream, "reconfigure", None)
    if reconfigure is not None:
        reconfigure(encoding="utf-8", errors="replace")


REPO_ROOT = Path(__file__).resolve().parent.parent
# ADR-009 sibling 拓扑：OrangeRender / Orange-Wiki 在本仓 sibling，由 Orange-Ecosystem 持有
SIBLING_RENDER = REPO_ROOT.parent / "OrangeRender"
SIBLING_WIKI = REPO_ROOT.parent / "Orange-Wiki"
# DragonBones 是引擎私有运行时依赖，保留 in-tree vendor submodule
VENDOR_DRAGONBONES = REPO_ROOT / "vendor" / "DragonBones"

DEFAULT_DEP_PREFIX = Path(r"D:\3rdparty")
DEFAULT_RENDER_SDK = Path(r"D:\sdk\orange-render")
DEFAULT_BUILD_DIR = REPO_ROOT / "build"
DEFAULT_RENDER_BUILD_DIR = SIBLING_RENDER / "build"

WIKI_REQUIRED_BRANCH = "Orange-Render-Wiki"


# ---------------------------------------------------------------------------
# 工具函数
# ---------------------------------------------------------------------------

def _run(cmd: Sequence[str], *, cwd: Path | None = None, env: dict[str, str] | None = None) -> None:
    printable = " ".join(str(c) for c in cmd)
    where = f" (cwd={cwd})" if cwd else ""
    print(f"[exec]{where} {printable}", flush=True)
    subprocess.run(list(cmd), check=True, cwd=str(cwd) if cwd else None, env=env)


def _capture(cmd: Sequence[str], *, cwd: Path | None = None) -> str:
    result = subprocess.run(
        list(cmd),
        check=True,
        cwd=str(cwd) if cwd else None,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def _which(tool: str) -> str:
    found = shutil.which(tool)
    if not found:
        raise RuntimeError(f"未在 PATH 中找到 {tool}，请先安装并加入 PATH。")
    return found


def _default_generator() -> str:
    if sys.platform == "win32":
        return "Visual Studio 17 2022"
    if sys.platform == "darwin":
        return "Xcode"
    return "Ninja"


def _is_multi_config(generator: str) -> bool:
    g = generator.lower()
    return "visual studio" in g or "xcode" in g


def _section(title: str) -> None:
    bar = "=" * (len(title) + 8)
    print(f"\n{bar}\n=== {title} ===\n{bar}", flush=True)


# ---------------------------------------------------------------------------
# vendor SHA cache + force-relink helpers
# ---------------------------------------------------------------------------
#
# 解决 incremental link trap（详见 memory reference_msvc_incremental_link_mtime_trap.md
# 与本脚本 2026-05-19 验证记录）。完整 trap 链：
#
#   1. git pull / git checkout 不更新 vendor 源文件 mtime
#   2. MSBuild 看 src mtime < .obj mtime → 跳过 compile（.obj 不更新）
#   3. .obj 不变 → 跳过 link（.lib 不更新）
#   4. cmake install 走 copy_if_different 保留 mtime → install/.lib 不更新
#   5. 引擎 build 看 install/.lib mtime < .exe mtime → 跳过 re-link → .exe 不更新
#   6. 启动崩溃：.exe 链接的是 vendor 旧版本（含早已 fix 的 bug）
#
# 修复策略（三钩子）：
#
#   H1 _detect_vendor_changes  —— 对比 build/.vendor-sha-cache.json vs 当前 vendor
#                                 HEAD，决定哪些 vendor 需要 force rebuild
#   H2 _bump_lib_mtimes        —— vendor build/install 之后把 install/.lib mtime
#                                 改为 now，对抗 copy_if_different trap
#   H3 _force_relink_cleanup   —— 引擎 build 之前删 build/bin/<config>/*.exe，
#                                 强制 MSBuild 走完整 link 路径
#
# 钩子组合（SHA 未变化时全部 no-op，开发期增量 build 0 额外成本）。
# --force-vendor-rebuild flag 显式跳过 SHA 对比，CI / 怀疑 trap 时手动触发。

_VENDOR_SHA_CACHE_FILENAME = ".vendor-sha-cache.json"


def _vendor_sha(vendor_path: Path) -> str:
    """返回 vendor 当前 HEAD SHA；working tree 有未提交改动时追加 '-dirty' 后缀。

    dirty 检测是必要的：本地改了 vendor 源码但没 commit 时，HEAD SHA 不变但需要
    re-build。'-dirty' 后缀让 cache 比对自然失败 → 触发 force rebuild。
    """
    if not (vendor_path / ".git").exists():
        return "no-git"
    try:
        sha = _capture(["git", "-C", str(vendor_path), "rev-parse", "HEAD"])
        dirty = _capture(["git", "-C", str(vendor_path), "status", "--porcelain"])
    except subprocess.CalledProcessError:
        return "unknown"
    return sha + ("-dirty" if dirty else "")


def _sha_cache_file(build_dir: Path) -> Path:
    return build_dir / _VENDOR_SHA_CACHE_FILENAME


def _load_sha_cache(build_dir: Path) -> dict:
    cache_file = _sha_cache_file(build_dir)
    if not cache_file.is_file():
        return {}
    try:
        return json.loads(cache_file.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}


def _save_sha_cache(build_dir: Path, data: dict) -> None:
    cache_file = _sha_cache_file(build_dir)
    cache_file.parent.mkdir(parents=True, exist_ok=True)
    cache_file.write_text(
        json.dumps(data, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def _detect_vendor_changes(build_dir: Path, force: bool) -> tuple[set, dict]:
    """检测哪些 vendor 需要 force rebuild。

    返回 (needs_rebuild 集合, 当前 vendor SHA 字典)。SHA 字典在 step 5 成功后通过
    _save_sha_cache 写回，作为下次 build 的对比基准。

    当前只跟踪 OrangeRender —— 它走 build_all.py Step 4 的 cmake install 路径，
    是 trap 的主要受害者。DragonBones 是 in-tree 编译走 engine build 增量逻辑，
    Orange-Wiki 是知识库不影响 build —— 未来按需扩展本字典即可纳入。

    ADR-009 后 OrangeRender 在 sibling 目录而非 vendor/，本字典 path 同步更新。
    """
    vendors = {
        "OrangeRender": SIBLING_RENDER,
    }
    cache = _load_sha_cache(build_dir)
    current = {name: _vendor_sha(path) for name, path in vendors.items()}
    needs = set()
    for name, sha in current.items():
        cached = cache.get(name)
        short_sha = sha[:7] if sha and sha != "no-git" else sha
        short_cached = cached[:7] if cached and cached != "no-git" else (cached or "none")
        if force:
            print(f"[vendor] {name}: force rebuild (sha={short_sha})", flush=True)
            needs.add(name)
        elif cached != sha:
            print(
                f"[vendor] {name}: SHA changed ({short_cached} → {short_sha}) → force rebuild",
                flush=True,
            )
            needs.add(name)
        else:
            print(f"[vendor] {name}: SHA unchanged ({short_sha}) → incremental ok", flush=True)
    return needs, current


def _bump_lib_mtimes(lib_dir: Path) -> int:
    """把 lib 目录里所有 .lib / .a 文件 mtime 改为 now。

    对抗 cmake install --copy-if-different 保留源 mtime 的 trap：vendor 真的
    re-build 了，但 install 出来的 .lib mtime 仍是源 .lib 的旧 mtime；下游
    MSBuild 看 install/.lib mtime 旧于 .exe → 跳过 re-link。
    """
    if not lib_dir.is_dir():
        return 0
    bumped = 0
    for ext in ("*.lib", "*.a"):
        for lib in lib_dir.glob(ext):
            try:
                os.utime(lib, None)
                bumped += 1
            except OSError as e:
                print(f"[vendor] WARN: could not bump mtime on {lib}: {e}", flush=True)
    return bumped


def _force_relink_cleanup(build_dir: Path, config: str) -> int:
    """删 build/bin/<config>/*.exe，强制 MSBuild 走完整 link 路径。

    单凭 _bump_lib_mtimes 在某些 MSBuild 版本 / 缓存状态下仍不够（例如 .ilk /
    .pdb 增量 link 数据库判定为 up-to-date）；删 .exe 是绕过 MSBuild 增量判定
    的最直接路径——目标产物不存在时 MSBuild 必须重新 link。
    """
    bin_dir = build_dir / "bin" / config
    if not bin_dir.is_dir():
        return 0
    removed = 0
    for exe in bin_dir.glob("*.exe"):
        try:
            exe.unlink()
            print(f"[force-relink] removed {exe.name}", flush=True)
            removed += 1
        except OSError as e:
            print(f"[force-relink] WARN: could not remove {exe.name}: {e}", flush=True)
    return removed


# ---------------------------------------------------------------------------
# Step 1: submodule
# ---------------------------------------------------------------------------

def _sync_submodules(git: str) -> None:
    _section("Step 1 / 同步本仓 git submodule + sibling sanity check（ADR-009 拓扑）")

    # 本仓 .gitmodules 仅含 vendor/DragonBones 一段（ADR-009 后 OrangeRender /
    # Orange-Wiki 改走 sibling 拓扑由 Orange-Ecosystem 持有）。--recursive 仍保留
    # 以便 DragonBones 或未来其它 in-tree vendor 可能含 nested submodule 的兼容。
    _run([git, "submodule", "sync", "--recursive"], cwd=REPO_ROOT)
    _run([git, "submodule", "update", "--init", "--recursive"], cwd=REPO_ROOT)

    # sibling Orange-Wiki 存在性 + 分支校验（不再 git submodule update —— 它已不是
    # 本仓 submodule；维护它的责任在 Orange-Ecosystem umbrella 仓那侧）
    if not SIBLING_WIKI.is_dir():
        raise RuntimeError(
            f"sibling 'Orange-Wiki' 不存在: {SIBLING_WIKI}\n"
            f"ADR-009 后 Orange-Wiki 改走 sibling 拓扑（不再是本仓 submodule），\n"
            f"典型来源是 git clone --recursive https://github.com/JZNMCO55/Orange-Ecosystem.git；\n"
            f"或手工 clone 到 {SIBLING_WIKI}。"
        )
    try:
        current = _capture([git, "branch", "--show-current"], cwd=SIBLING_WIKI)
    except subprocess.CalledProcessError:
        current = ""
    if current != WIKI_REQUIRED_BRANCH:
        print(
            f"[info] Orange-Wiki 当前分支='{current or '<detached>'}', "
            f"切到 '{WIKI_REQUIRED_BRANCH}'",
            flush=True,
        )
        _run([git, "fetch", "origin", WIKI_REQUIRED_BRANCH], cwd=SIBLING_WIKI)
        _run([git, "checkout", WIKI_REQUIRED_BRANCH], cwd=SIBLING_WIKI)
        _run([git, "pull", "--ff-only", "origin", WIKI_REQUIRED_BRANCH], cwd=SIBLING_WIKI)
    else:
        print(f"[ok] Orange-Wiki 已在 '{WIKI_REQUIRED_BRANCH}'", flush=True)

    # 存在性 sanity check：OR/Wiki 走 sibling，DragonBones 仍走 vendor
    for label, path, kind in (
        ("OrangeRender", SIBLING_RENDER, "sibling"),
        ("Orange-Wiki", SIBLING_WIKI, "sibling"),
        ("DragonBones", VENDOR_DRAGONBONES, "vendor"),
    ):
        if not path.is_dir() or not any(path.iterdir()):
            hint = ""
            if kind == "sibling":
                hint = (
                    f"\nADR-009 拓扑前置：sibling {label} 必须存在于 {path}。"
                    f"\n典型来源是从 Orange-Ecosystem umbrella 仓 clone --recursive。"
                )
            raise RuntimeError(
                f"{kind} '{label}' 未正确初始化，目录 {path} 不存在或为空。{hint}"
            )
        print(f"[ok] {kind}/{label} 已就绪 ({path})", flush=True)


# ---------------------------------------------------------------------------
# Step 2 / 3: 第三方依赖
# ---------------------------------------------------------------------------

def _run_fetch_script(label: str, script: Path, prefix: Path, jobs: int, config: str,
                      generator: str, with_spdlog: bool) -> None:
    if not script.is_file():
        raise RuntimeError(f"未找到 {label} 的 fetch 脚本：{script}")
    cmd = [
        sys.executable, str(script),
        "--prefix", str(prefix),
        "--jobs", str(jobs),
        "--config", config,
        "--generator", generator,
    ]
    # OrangeRender 的 fetch 脚本支持 --with-spdlog；engine 端不暴露此 flag
    # （由 3rdparty.json 的 optional + gate 控制），所以只给 render 传。
    if label == "OrangeRender" and with_spdlog:
        cmd.append("--with-spdlog")
    _run(cmd, cwd=REPO_ROOT)


def _fetch_render_3rdparty(prefix: Path, jobs: int, config: str, generator: str,
                           with_spdlog: bool) -> None:
    _section("Step 2 / 拉取并编译 OrangeRender 的第三方依赖")
    _run_fetch_script(
        "OrangeRender",
        SIBLING_RENDER / "scripts" / "fetch_and_build_3rdparty.py",
        prefix, jobs, config, generator, with_spdlog,
    )


def _fetch_engine_3rdparty(prefix: Path, jobs: int, config: str, generator: str,
                           with_spdlog: bool) -> None:
    _section("Step 3 / 拉取并编译 OrangeEngine 的第三方依赖")
    _run_fetch_script(
        "OrangeEngine",
        REPO_ROOT / "scripts" / "fetch_and_build_3rdparty.py",
        prefix, jobs, config, generator, with_spdlog,
    )


# ---------------------------------------------------------------------------
# Step 4: 编 + install OrangeRender
# ---------------------------------------------------------------------------

def _build_and_install_render(dep_prefix: Path, sdk_prefix: Path, build_dir: Path,
                              config: str, generator: str, jobs: int,
                              clean: bool, with_spdlog: bool) -> None:
    _section("Step 4 / 编译并安装 OrangeRender 到 SDK 前缀")
    build_py = SIBLING_RENDER / "build.py"
    if not build_py.is_file():
        raise RuntimeError(f"未找到 {build_py}")

    cmd = [
        sys.executable, str(build_py),
        "--prefix", str(dep_prefix),
        "--build-dir", str(build_dir),
        "--config", config,
        "--generator", generator,
        "--jobs", str(jobs),
        "--install",
        "--install-prefix", str(sdk_prefix),
    ]
    if clean:
        cmd.append("--clean")
    if with_spdlog:
        cmd.append("--with-spdlog")
    _run(cmd, cwd=REPO_ROOT)


# ---------------------------------------------------------------------------
# Step 5: 编 OrangeEngine
# ---------------------------------------------------------------------------

def _build_engine(cmake: str, dep_prefix: Path, sdk_prefix: Path, build_dir: Path,
                  config: str, generator: str, jobs: int, clean: bool,
                  with_spdlog: bool, with_tests: bool, run_tests: bool,
                  with_samples: bool) -> None:
    _section("Step 5 / 配置并编译 OrangeEngine（含 OrangeEditor；samples/tests 默认关闭）")

    if clean and build_dir.exists():
        print(f"[info] 删除引擎构建目录: {build_dir}", flush=True)
        shutil.rmtree(build_dir)
    build_dir.mkdir(parents=True, exist_ok=True)

    # CMAKE_PREFIX_PATH 同时含第三方安装 + OrangeRender SDK
    dep_install = dep_prefix / "install"
    prefix_path_value = f"{dep_install};{sdk_prefix}"

    multi_config = _is_multi_config(generator)
    configure_cmd = [
        cmake,
        "-S", str(REPO_ROOT),
        "-B", str(build_dir),
        "-G", generator,
        f"-DCMAKE_PREFIX_PATH={prefix_path_value}",
        f"-DORANGE_ENGINE_BUILD_TESTS={'ON' if with_tests else 'OFF'}",
        f"-DORANGE_ENGINE_BUILD_SAMPLES={'ON' if with_samples else 'OFF'}",
        f"-DORANGE_ENGINE_WITH_SPDLOG={'ON' if with_spdlog else 'OFF'}",
    ]
    if sys.platform == "win32" and "visual studio" in generator.lower():
        configure_cmd += ["-A", "x64"]
    if not multi_config:
        configure_cmd.append(f"-DCMAKE_BUILD_TYPE={config}")

    build_cmd = [cmake, "--build", str(build_dir), "--parallel", str(jobs)]
    if multi_config:
        build_cmd += ["--config", config]

    env = os.environ.copy()
    prev_prefix = env.get("CMAKE_PREFIX_PATH", "")
    env["CMAKE_PREFIX_PATH"] = prefix_path_value + (os.pathsep + prev_prefix if prev_prefix else "")

    _run(configure_cmd, env=env)
    _run(build_cmd, env=env)

    if run_tests:
        if not with_tests:
            print(
                "[warn] --run-tests 但未带 --with-tests；ctest 目录可能为空。",
                file=sys.stderr,
            )
        ctest_cmd = ["ctest", "--test-dir", str(build_dir), "--output-on-failure"]
        if multi_config:
            ctest_cmd += ["-C", config]
        _run(ctest_cmd, env=env)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def _parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="一键构建 OrangeEngine（含子模块同步、第三方依赖、OrangeRender 安装、引擎与编辑器编译）。",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    p.add_argument("--prefix", type=Path, default=DEFAULT_DEP_PREFIX,
                   help=f"第三方依赖根目录（脚本会装到 <prefix>/install；默认 {DEFAULT_DEP_PREFIX}）")
    p.add_argument("--render-sdk", type=Path, default=DEFAULT_RENDER_SDK,
                   help=f"OrangeRender 的 install 前缀，引擎 find_package 时通过 CMAKE_PREFIX_PATH 解析（默认 {DEFAULT_RENDER_SDK}）")
    p.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR,
                   help=f"OrangeEngine 构建目录（默认 {DEFAULT_BUILD_DIR}）")
    p.add_argument("--render-build-dir", type=Path, default=DEFAULT_RENDER_BUILD_DIR,
                   help=f"OrangeRender 构建目录（默认 {DEFAULT_RENDER_BUILD_DIR}）")
    p.add_argument("--config", choices=("Debug", "Release", "RelWithDebInfo", "MinSizeRel"),
                   default="Debug", help="构建配置（默认 Debug）")
    p.add_argument("--generator", "-G", default=_default_generator(),
                   help=f"CMake 生成器（默认 {_default_generator()}）")
    p.add_argument("--jobs", "-j", type=int, default=max(1, (os.cpu_count() or 4)),
                   help="并行编译线程数")

    p.add_argument("--with-spdlog", action="store_true",
                   help="启用 spdlog 后端（OrangeRender 与 OrangeEngine 同时打开 gate）")
    p.add_argument("--with-tests", action="store_true",
                   help="为 OrangeEngine 打开 ORANGE_ENGINE_BUILD_TESTS（默认关闭）")
    p.add_argument("--run-tests", action="store_true",
                   help="编译完成后跑一次 ctest（建议同时 --with-tests）")
    p.add_argument("--with-samples", action="store_true",
                   help="为 OrangeEngine 打开 ORANGE_ENGINE_BUILD_SAMPLES（默认关闭，加快编辑器迭代）")
    p.add_argument("--clean", action="store_true",
                   help="清理 OrangeRender 与 OrangeEngine 的构建目录后从头来")
    p.add_argument("--force-vendor-rebuild", action="store_true",
                   help="跳过 vendor SHA 比对，强制走 force rebuild 路径（Step 4 自动 --clean + bump install lib mtime + 删下游 .exe）。CI / 怀疑 incremental link trap 时手动触发")

    p.add_argument("--skip-submodules", action="store_true", help="跳过 Step 1（git submodule）")
    p.add_argument("--skip-render-3rdparty", action="store_true", help="跳过 Step 2（OrangeRender 3rdparty）")
    p.add_argument("--skip-engine-3rdparty", action="store_true", help="跳过 Step 3（OrangeEngine 3rdparty）")
    p.add_argument("--skip-3rdparty", action="store_true",
                   help="同时跳过 Step 2 和 Step 3（增量构建场景下用）")
    p.add_argument("--skip-render", action="store_true", help="跳过 Step 4（OrangeRender build/install）")
    p.add_argument("--skip-engine", action="store_true", help="跳过 Step 5（OrangeEngine build）")

    return p.parse_args(argv)


def main(argv: list[str]) -> int:
    args = _parse_args(argv)

    try:
        git = _which("git")
        cmake = _which("cmake")
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 1

    if not os.environ.get("VULKAN_SDK"):
        print(
            "警告: 未检测到环境变量 VULKAN_SDK。OrangeRender 的依赖（volk/VMA）"
            "或渲染器自身配置阶段可能失败。",
            file=sys.stderr,
        )

    dep_prefix = args.prefix.resolve()
    render_sdk = args.render_sdk.resolve()
    engine_build = args.build_dir.resolve()
    render_build = args.render_build_dir.resolve()

    print(f"[plan] 第三方依赖前缀     : {dep_prefix}", flush=True)
    print(f"[plan] OrangeRender SDK : {render_sdk}", flush=True)
    print(f"[plan] OrangeRender 构建 : {render_build}", flush=True)
    print(f"[plan] OrangeEngine 构建 : {engine_build}", flush=True)
    print(f"[plan] 生成器/配置/并发 : {args.generator} / {args.config} / -j{args.jobs}", flush=True)

    skip_render_3rd = args.skip_render_3rdparty or args.skip_3rdparty
    skip_engine_3rd = args.skip_engine_3rdparty or args.skip_3rdparty

    try:
        if not args.skip_submodules:
            _sync_submodules(git)
        else:
            print("[skip] Step 1 (submodule)", flush=True)

        # H1：vendor SHA 变更检测（详见上方 helpers 段头注释）。
        # 始终运行——SHA 未变化时 needs_rebuild 为空集合，所有钩子 no-op；
        # 变化时驱动 H2 / H3 钩子在 Step 4 / Step 5 期间做强制 rebuild + relink。
        needs_rebuild, vendor_shas = _detect_vendor_changes(
            engine_build, args.force_vendor_rebuild
        )
        render_force_clean = ("OrangeRender" in needs_rebuild)
        render_clean = args.clean or render_force_clean

        if not skip_render_3rd:
            _fetch_render_3rdparty(dep_prefix, args.jobs, args.config, args.generator, args.with_spdlog)
        else:
            print("[skip] Step 2 (OrangeRender 3rdparty)", flush=True)

        if not skip_engine_3rd:
            _fetch_engine_3rdparty(dep_prefix, args.jobs, args.config, args.generator, args.with_spdlog)
        else:
            print("[skip] Step 3 (OrangeEngine 3rdparty)", flush=True)

        if not args.skip_render:
            if render_force_clean and not args.clean:
                print(
                    "[vendor] OrangeRender SHA changed → Step 4 强制 --clean（force rebuild）",
                    flush=True,
                )
            _build_and_install_render(
                dep_prefix, render_sdk, render_build,
                args.config, args.generator, args.jobs,
                render_clean, args.with_spdlog,
            )
            # H2：bump install lib mtime 对抗 copy_if_different trap。两个候选 prefix
            # 都 bump，避免本机有多份 install（D:/sdk/orange-render + D:/3rdparty/install）
            # 时遗漏 cmake 实际 find_package 命中的那份。
            if "OrangeRender" in needs_rebuild:
                bumped = _bump_lib_mtimes(render_sdk / "lib")
                bumped += _bump_lib_mtimes(dep_prefix / "install" / "lib")
                if bumped:
                    print(
                        f"[vendor] bumped {bumped} install lib mtime(s) to defeat copy_if_different trap",
                        flush=True,
                    )
        else:
            print("[skip] Step 4 (OrangeRender build/install)", flush=True)

        if not args.skip_engine:
            # H3：删 .exe 强制 MSBuild re-link。
            # 只在 needs_rebuild 非空时触发——SHA 未变化的 incremental 路径仍走快速增量。
            if needs_rebuild:
                removed = _force_relink_cleanup(engine_build, args.config)
                if removed:
                    print(
                        f"[force-relink] removed {removed} stale .exe(s) → MSBuild 必须重新 link",
                        flush=True,
                    )
            _build_engine(
                cmake, dep_prefix, render_sdk, engine_build,
                args.config, args.generator, args.jobs,
                args.clean, args.with_spdlog, args.with_tests, args.run_tests,
                args.with_samples,
            )
            # H3 post：写回 SHA cache，作为下次 build 的基准。
            # 只在 Step 5 成功（异常路径不在此分支）时写——确保 cache 里的 SHA 对应
            # 当前 build 产物所基于的源码版本。
            _save_sha_cache(engine_build, vendor_shas)
        else:
            print("[skip] Step 5 (OrangeEngine build)", flush=True)

    except subprocess.CalledProcessError as exc:
        print(f"\n失败：子命令退出码 {exc.returncode}", file=sys.stderr)
        return exc.returncode or 1
    except RuntimeError as exc:
        print(f"\n失败：{exc}", file=sys.stderr)
        return 1

    _section("完成")
    bin_dir = engine_build / 'bin' / args.config
    print(f"- OrangeEngine 二进制    : {bin_dir}", flush=True)
    print(f"  (OrangeEditor         : {bin_dir / 'OrangeEditor.exe'})", flush=True)
    if args.with_samples:
        print(f"  (samples/01_minimal   : {bin_dir / '01_minimal_window.exe'})", flush=True)
    else:
        print("  (samples 默认未编译；若需要请加 --with-samples)", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
