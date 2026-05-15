#!/usr/bin/env python3
"""
OrangeEngine 一键编译入口。

把"从 0 到 OrangeEditor 跑起来"所需的全部步骤串成单脚本：

  1. 同步 git submodule（vendor/OrangeRender、vendor/Orange-Wiki、vendor/DragonBones）
     - 自动校验 Orange-Wiki 是否在 `Orange-Render-Wiki` 分支（CLAUDE.md 硬要求），
       不在就切过去。
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
VENDOR_RENDER = REPO_ROOT / "vendor" / "OrangeRender"
VENDOR_WIKI = REPO_ROOT / "vendor" / "Orange-Wiki"
VENDOR_DRAGONBONES = REPO_ROOT / "vendor" / "DragonBones"

DEFAULT_DEP_PREFIX = Path(r"D:\3rdparty")
DEFAULT_RENDER_SDK = Path(r"D:\sdk\orange-render")
DEFAULT_BUILD_DIR = REPO_ROOT / "build"
DEFAULT_RENDER_BUILD_DIR = VENDOR_RENDER / "build"

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
# Step 1: submodule
# ---------------------------------------------------------------------------

def _sync_submodules(git: str) -> None:
    _section("Step 1 / 同步 git submodule")

    # `git submodule update --init --recursive` 会一次性把 .gitmodules 里
    # 注册的全部 submodule 拉下来；递归是为了 OrangeRender 自身可能也有
    # nested submodule 的将来兼容。
    _run([git, "submodule", "sync", "--recursive"], cwd=REPO_ROOT)
    _run([git, "submodule", "update", "--init", "--recursive"], cwd=REPO_ROOT)

    # Orange-Wiki 必须在 Orange-Render-Wiki 分支（CLAUDE.md 硬要求；
    # default main 不含 wiki 内容）。submodule 默认 detached HEAD，需要显式
    # checkout 一次。
    if VENDOR_WIKI.is_dir():
        try:
            current = _capture([git, "branch", "--show-current"], cwd=VENDOR_WIKI)
        except subprocess.CalledProcessError:
            current = ""
        if current != WIKI_REQUIRED_BRANCH:
            print(
                f"[info] Orange-Wiki 当前分支='{current or '<detached>'}', "
                f"切到 '{WIKI_REQUIRED_BRANCH}'",
                flush=True,
            )
            _run([git, "fetch", "origin", WIKI_REQUIRED_BRANCH], cwd=VENDOR_WIKI)
            _run([git, "checkout", WIKI_REQUIRED_BRANCH], cwd=VENDOR_WIKI)
            _run([git, "pull", "--ff-only", "origin", WIKI_REQUIRED_BRANCH], cwd=VENDOR_WIKI)
        else:
            print(f"[ok] Orange-Wiki 已在 '{WIKI_REQUIRED_BRANCH}'", flush=True)

    # 给主要 vendor 目录打一个存在性 sanity check，方便日后路径漂移时早爆
    for label, path in (
        ("OrangeRender", VENDOR_RENDER),
        ("Orange-Wiki", VENDOR_WIKI),
        ("DragonBones", VENDOR_DRAGONBONES),
    ):
        if not path.is_dir() or not any(path.iterdir()):
            raise RuntimeError(
                f"submodule '{label}' 未正确初始化，目录 {path} 不存在或为空。"
            )
        print(f"[ok] vendor/{label} 已就绪 ({path})", flush=True)


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
        VENDOR_RENDER / "scripts" / "fetch_and_build_3rdparty.py",
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
    build_py = VENDOR_RENDER / "build.py"
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

        if not skip_render_3rd:
            _fetch_render_3rdparty(dep_prefix, args.jobs, args.config, args.generator, args.with_spdlog)
        else:
            print("[skip] Step 2 (OrangeRender 3rdparty)", flush=True)

        if not skip_engine_3rd:
            _fetch_engine_3rdparty(dep_prefix, args.jobs, args.config, args.generator, args.with_spdlog)
        else:
            print("[skip] Step 3 (OrangeEngine 3rdparty)", flush=True)

        if not args.skip_render:
            _build_and_install_render(
                dep_prefix, render_sdk, render_build,
                args.config, args.generator, args.jobs,
                args.clean, args.with_spdlog,
            )
        else:
            print("[skip] Step 4 (OrangeRender build/install)", flush=True)

        if not args.skip_engine:
            _build_engine(
                cmake, dep_prefix, render_sdk, engine_build,
                args.config, args.generator, args.jobs,
                args.clean, args.with_spdlog, args.with_tests, args.run_tests,
                args.with_samples,
            )
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
