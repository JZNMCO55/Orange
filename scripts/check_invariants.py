#!/usr/bin/env python3
# OrangeEngine 项目级 invariant lint 脚本。
# 覆盖 CLAUDE.md "Design guardrails" + "OrangeEditor 架构纪律" 节的硬纪律。
# 退出码：0 = 全部通过；1 = 至少一条违规。
#
# 用法：
#   python scripts/check_invariants.py              # 检查整个仓库
#   python scripts/check_invariants.py --paths a b  # 仅检查指定文件 / 目录
#   python scripts/check_invariants.py --json       # 机器可读输出（CI 用）
#
# 推荐：commit 前手动跑；CI 自动跑（pre-commit hook 见 scripts/install_hooks.py 或手动 wire）。

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Iterable

# Windows PowerShell 默认 cp936，对中文 / emoji 报错。强制 UTF-8 输出。
try:
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
except Exception:
    pass

REPO_ROOT = Path(__file__).resolve().parent.parent

# 文件后缀白名单：仅扫描 C++ 源码 / 头文件。
CXX_EXTS = {".h", ".hpp", ".hh", ".inl", ".cpp", ".cc", ".cxx"}

# 排除路径前缀（相对仓库根）：第三方 / build / 历史归档。
EXCLUDE_PREFIXES = (
    "vendor/",
    "build/",
    "build-",
    ".git/",
    "docs/Technical Documentation/",  # 历史归档，按 CLAUDE.md 保留
)


@dataclass
class Violation:
    rule: str
    path: str
    line: int
    message: str

    def render(self) -> str:
        return f"{self.path}:{self.line}: [{self.rule}] {self.message}"


@dataclass
class Rule:
    name: str
    description: str
    check: Callable[[Path, list[str]], Iterable[Violation]]


# ---------- helpers ---------------------------------------------------------


def iter_cxx_files(roots: Iterable[Path]) -> Iterable[Path]:
    seen: set[Path] = set()
    for root in roots:
        if root.is_file():
            if root.suffix in CXX_EXTS:
                yield root
            continue
        for p in root.rglob("*"):
            if not p.is_file() or p.suffix not in CXX_EXTS:
                continue
            rel = p.relative_to(REPO_ROOT).as_posix()
            if any(rel.startswith(pref) for pref in EXCLUDE_PREFIXES):
                continue
            if p in seen:
                continue
            seen.add(p)
            yield p


def rel(p: Path) -> str:
    return p.relative_to(REPO_ROOT).as_posix()


# ---------- rules -----------------------------------------------------------


# Header isolation：第三方 / 渲染器头只允许出现在指定 src 子目录。
HEADER_ISOLATION_MAP: list[tuple[re.Pattern[str], str, str]] = [
    (
        re.compile(r'^\s*#\s*include\s*[<"](orange/(?:renderer|rhi)/[^>"]+)[>"]'),
        "src/render/",
        "OrangeRender 头仅允许在 src/render/**",
    ),
    (
        re.compile(r'^\s*#\s*include\s*[<"](box2d(?:/[^>"]+)?|Box2D(?:/[^>"]+)?)[>"]'),
        "src/physics/box2d/",
        "Box2D 头仅允许在 src/physics/box2d/**",
    ),
    (
        re.compile(r'^\s*#\s*include\s*[<"](dragonBones/[^>"]+)[>"]'),
        "src/animation/dragonbones/",
        "DragonBones 头仅允许在 src/animation/dragonbones/**",
    ),
    (
        re.compile(r'^\s*#\s*include\s*[<"](miniaudio\.h)[>"]'),
        "src/audio/miniaudio/",
        "miniaudio 头仅允许在 src/audio/miniaudio/**",
    ),
    (
        re.compile(r'^\s*#\s*include\s*[<"](vulkan/[^>"]+|volk\.h|vk_mem_alloc\.h)[>"]'),
        "src/",  # 任何 src/ 子目录都允许，但 include/ 公共头不允许
        "Vulkan/volk/VMA 头不得出现在公共头（include/orange/engine/**）",
    ),
]


def rule_header_isolation(path: Path, lines: list[str]) -> Iterable[Violation]:
    rel_path = rel(path)
    # 仅检查仓库内 include/ + src/ + tools/ + samples/。
    in_public_header = rel_path.startswith("include/orange/engine/")
    in_src = rel_path.startswith("src/")
    if not (in_public_header or in_src or rel_path.startswith("tools/") or rel_path.startswith("samples/")):
        return
    for i, line in enumerate(lines, start=1):
        for pattern, allowed_prefix, message in HEADER_ISOLATION_MAP:
            m = pattern.match(line)
            if not m:
                continue
            included = m.group(1)
            # Vulkan 规则特殊：禁止出现在公共头，src 内任意位置允许。
            if "vulkan" in pattern.pattern or "volk" in pattern.pattern:
                if in_public_header:
                    yield Violation(
                        "header-isolation",
                        rel_path,
                        i,
                        f"公共头不得 include `{included}`：{message}",
                    )
                continue
            # 其他规则：仅在 src/ 内的指定子目录允许；公共头一律禁止。
            if in_public_header:
                yield Violation(
                    "header-isolation",
                    rel_path,
                    i,
                    f"公共头不得 include `{included}`：{message}",
                )
            elif in_src and not rel_path.startswith(allowed_prefix):
                yield Violation(
                    "header-isolation",
                    rel_path,
                    i,
                    f"`{included}` 仅允许在 `{allowed_prefix}**` 下使用",
                )


# 公共头不得直接调用 nlohmann::json —— 必须走 Core::Serialization 包装。
NLOHMANN_JSON_RE = re.compile(r"\bnlohmann\s*::\s*json\b")


def rule_no_bare_json_in_public_headers(path: Path, lines: list[str]) -> Iterable[Violation]:
    rel_path = rel(path)
    if not rel_path.startswith("include/orange/engine/"):
        return
    for i, line in enumerate(lines, start=1):
        # 跳过单行注释行
        stripped = line.lstrip()
        if stripped.startswith("//") or stripped.startswith("*"):
            continue
        if NLOHMANN_JSON_RE.search(line):
            yield Violation(
                "no-bare-json-in-public-headers",
                rel_path,
                i,
                "公共头不得直接出现 `nlohmann::json` —— 走 Core::Serialization 包装",
            )


# 源码不得引用 Task NN / Phase N 字样（CLAUDE.md "No task references in code"）。
TASK_REF_RE = re.compile(r"\b(?:Task|Phase)\s+\d+(?:[.-]\d+)?", re.IGNORECASE)
# 注释里才检查；字符串字面量豁免（如调试日志里偶尔提到 phase 是允许的）。
COMMENT_LINE_RE = re.compile(r"^\s*(?://|\*|/\*)")


def _block_comment_starts_here(line: str) -> bool:
    """判断 line 是否开启一段跨行 block comment (/* ... 没在本行 */)。

    需排除：(1) `/*` 出现在 `//` 单行注释之后；(2) `/*` 出现在字符串字面量内。
    这两类对 block-comment 状态机不构成影响——之前的实现把它们也当成起点，
    导致后续代码行被误判进 block-comment 状态，进而把字符串字面量里的
    `Task NN` 当成注释里的 task ref 误报。
    """
    in_string = False
    i = 0
    n = len(line)
    while i < n - 1:
        c = line[i]
        if c == '"' and (i == 0 or line[i - 1] != "\\"):
            in_string = not in_string
            i += 1
            continue
        if not in_string:
            two = line[i : i + 2]
            if two == "//":
                # 单行注释，剩余部分里的 /* 不算 block-comment 起点
                return False
            if two == "/*":
                # 本行内有匹配的 */ 吗？如果有，整段被 /* */ 包住，不跨行
                close = line.find("*/", i + 2)
                return close == -1
        i += 1
    return False


def rule_no_task_references(path: Path, lines: list[str]) -> Iterable[Violation]:
    rel_path = rel(path)
    # 仅检查代码目录；docs/ 当然允许。
    if not (
        rel_path.startswith("include/")
        or rel_path.startswith("src/")
        or rel_path.startswith("tools/")
        or rel_path.startswith("samples/")
    ):
        return
    in_block_comment = False
    for i, line in enumerate(lines, start=1):
        check_this = False
        if in_block_comment:
            check_this = True
            if "*/" in line:
                in_block_comment = False
        elif _block_comment_starts_here(line):
            in_block_comment = True
            check_this = True
        elif COMMENT_LINE_RE.match(line):
            check_this = True
        if not check_this:
            continue
        m = TASK_REF_RE.search(line)
        if m:
            yield Violation(
                "no-task-references",
                rel_path,
                i,
                f"代码注释禁止引用任务编号：`{m.group(0)}`（task / phase 元数据放 docs / commit message）",
            )


# OrangeEditor schema-first 纪律：v0.2.5 落地后，EditorRenderLayer 不得再有
# `DrawInspectorXxx` 模式 / EditorState 不得无脑挂字段。
# 本规则在 v0.2.5 完成前 报 warn-only（IDLE 状态）；完成后切 error。
DRAW_INSPECTOR_RE = re.compile(r"\bDrawInspector[A-Z][A-Za-z0-9_]+\b")


def rule_editor_no_hardcode(path: Path, lines: list[str]) -> Iterable[Violation]:
    rel_path = rel(path)
    if rel_path != "tools/OrangeEditor/EditorRenderLayer.h" and rel_path != "tools/OrangeEditor/EditorRenderLayer.cpp":
        return
    # 检测 marker 文件决定是否报 error
    marker = REPO_ROOT / "tools" / "OrangeEditor" / ".schema-first-locked"
    enforce = marker.exists()
    for i, line in enumerate(lines, start=1):
        if DRAW_INSPECTOR_RE.search(line):
            rule = "editor-no-hardcode" if enforce else "editor-no-hardcode-pending"
            severity = "禁止" if enforce else "v0.2.5 整骨后将禁止（当前 warn-only）"
            yield Violation(
                rule,
                rel_path,
                i,
                f"{severity}：`DrawInspectorXxx` 路径必须改 schema 驱动（见 CLAUDE.md 「OrangeEditor 架构纪律」）",
            )


# OrangeEditor v0.4.5 widget 像素列宽纪律：
#
# tools/OrangeEditor/ 内任何 `ImGui::SetNextItemWidth(<literal>)` /
# `ImGui::SetCursorPosX(<literal>)` / `ImGui::Indent(<literal>)` /
# `ImGui::Unindent(<literal>)` / `ImGui::SetColumnWidth(..., <literal>)`
# 调用，若括号内是字面量浮点 / 整数（不含 identifier / 派生表达式），
# 报违规。这些函数控制 widget 像素列宽 / 列偏移；硬编码字面量会在窄屏 /
# 高 DPI 撞列宽不足（v0.4 收尾撞过，详 editor-roadmap v0.4.5）。
#
# 允许的派生形式（不会被 match）：
#   * SetNextItemWidth(-FLT_MIN)       -- identifier
#   * SetNextItemWidth(dragW)          -- identifier
#   * SetNextItemWidth(ImGui::CalcTextSize("x").x + style.FramePadding.x * 2)
#                                       -- 表达式带 ImGui:: 或 style.*，识别为派生
#   * SetNextItemWidth(comboItemWidth(...))   -- 函数调用，识别为派生
#
# 简化策略：本规则只 hit "整个参数就是数字字面量 (可带 . 和 f 后缀)" 的情形；
# 任何表达式 / identifier 形式自动豁免。
EDITOR_PIXEL_LITERAL_FN = (
    "SetNextItemWidth", "SetCursorPosX", "SetCursorPosY",
    "Indent", "Unindent", "SetColumnWidth",
)
# 匹配 ImGui::<fn>(...) 内圆括号最外层；仅当整段参数是一个数字字面量时 hit。
# 数字字面量：[+-]?\d+ 或 [+-]?\d+\.\d+ 后可选 f/F 后缀；前后允许空白。
EDITOR_PIXEL_LITERAL_RE = re.compile(
    r"\bImGui::(" + "|".join(EDITOR_PIXEL_LITERAL_FN) + r")"
    r"\s*\(\s*([+-]?\d+(?:\.\d+)?[fF]?)\s*\)"
)


def rule_editor_no_widget_pixel_literal(path: Path, lines: list[str]) -> Iterable[Violation]:
    rel_path = rel(path)
    if not rel_path.startswith("tools/OrangeEditor/"):
        return
    in_block_comment = False
    for i, line in enumerate(lines, start=1):
        # 与 task-ref 规则同款 block-comment 状态机，避免注释里的样例 hit。
        if in_block_comment:
            if "*/" in line:
                in_block_comment = False
            continue
        if _block_comment_starts_here(line):
            in_block_comment = True
            continue
        if COMMENT_LINE_RE.match(line):
            continue
        m = EDITOR_PIXEL_LITERAL_RE.search(line)
        if not m:
            continue
        fn_name = m.group(1)
        literal = m.group(2)
        yield Violation(
            "editor-widget-pixel-literal",
            rel_path,
            i,
            f"`ImGui::{fn_name}({literal})` 用字面量像素：必须改 CalcTextSize / style.* / "
            "GetContentRegionAvail 派生（见 editor-roadmap v0.4.5 红线）",
        )


RULES: list[Rule] = [
    Rule("header-isolation", "公共头与第三方头隔离（box2d / dragonBones / miniaudio / orange/* / vulkan）", rule_header_isolation),
    Rule("no-bare-json-in-public-headers", "公共头禁止裸 nlohmann::json", rule_no_bare_json_in_public_headers),
    Rule("no-task-references", "代码注释禁止引用 Task NN / Phase N", rule_no_task_references),
    Rule("editor-no-hardcode", "OrangeEditor 禁止 DrawInspectorXxx hardcode（v0.2.5 后强制）", rule_editor_no_hardcode),
    Rule("editor-widget-pixel-literal", "OrangeEditor 禁止 widget 层字面量像素列宽（v0.4.5 后强制）", rule_editor_no_widget_pixel_literal),
]


# ---------- main ------------------------------------------------------------


BASELINE_PATH = REPO_ROOT / "scripts" / ".invariants-baseline.json"


def collect_violations(roots: list[Path]) -> list[Violation]:
    violations: list[Violation] = []
    for path in iter_cxx_files(roots):
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            # 偶有 UTF-8 BOM / GBK 文件——忽略而非崩溃，保留报告里。
            violations.append(
                Violation("io", rel(path), 0, "无法以 UTF-8 解码（建议转 UTF-8 后再检）")
            )
            continue
        lines = text.splitlines()
        for rule in RULES:
            violations.extend(rule.check(path, lines))
    return violations


def load_baseline() -> set[tuple[str, str, str]]:
    """读 baseline 文件，返回 (rule, path, message) 三元组集合用于过滤。

    刻意不存行号——历史文件后续编辑会移动行号，但只要同一文件 + 同一规则 +
    同一消息仍然存在，就视为已知（grandfathered）。这与 CLAUDE.md 立场一致：
    "Existing task-referencing banners in earlier files were a one-off pattern
    and are not to be propagated to new work" —— 既有的不强制重写，新增的不许加。
    """
    if not BASELINE_PATH.exists():
        return set()
    data = json.loads(BASELINE_PATH.read_text(encoding="utf-8"))
    return {(item["rule"], item["path"], item["message"]) for item in data.get("violations", [])}


def save_baseline(violations: list[Violation]) -> None:
    payload = {
        "doc": (
            "OrangeEngine invariant lint baseline. 仅记录历史 grandfathered "
            "违规——这些在 CLAUDE.md \"No task references in code\" 节明确豁免"
            "（既有 banner 不强制重写）。新增违规仍然 fail。重新生成："
            "python scripts/check_invariants.py --write-baseline"
        ),
        "violations": [
            {"rule": v.rule, "path": v.path, "message": v.message}
            for v in violations
        ],
    }
    BASELINE_PATH.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2, sort_keys=True),
        encoding="utf-8",
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="OrangeEngine invariant lint")
    parser.add_argument("--paths", nargs="*", default=None, help="检查指定文件/目录；默认整仓")
    parser.add_argument("--json", action="store_true", help="输出 JSON")
    parser.add_argument("--list-rules", action="store_true", help="打印规则清单并退出")
    parser.add_argument(
        "--write-baseline",
        action="store_true",
        help="把当前所有违规写进 baseline 文件（仅历史 grandfathered 时用）",
    )
    parser.add_argument(
        "--no-baseline",
        action="store_true",
        help="忽略 baseline 文件，报告所有违规（用于审计 / 清理 baseline）",
    )
    args = parser.parse_args(argv)

    if args.list_rules:
        for r in RULES:
            print(f"- {r.name}: {r.description}")
        return 0

    if args.paths:
        roots = [Path(p).resolve() for p in args.paths]
    else:
        roots = [REPO_ROOT / d for d in ("include", "src", "tools", "samples")]
        roots = [r for r in roots if r.exists()]

    violations = collect_violations(roots)

    if args.write_baseline:
        save_baseline(violations)
        print(f"Baseline written to {rel(BASELINE_PATH)}: {len(violations)} entries")
        return 0

    baseline = set() if args.no_baseline else load_baseline()
    new_violations = [
        v for v in violations if (v.rule, v.path, v.message) not in baseline
    ]
    grandfathered = len(violations) - len(new_violations)

    if args.json:
        payload = {
            "new_violations": [v.__dict__ for v in new_violations],
            "grandfathered_count": grandfathered,
        }
        print(json.dumps(payload, ensure_ascii=False, indent=2))
    else:
        for v in new_violations:
            print(v.render())
        if new_violations:
            print(
                f"\n{len(new_violations)} new violation(s) found "
                f"({grandfathered} grandfathered by baseline).",
                file=sys.stderr,
            )
        else:
            print(
                f"All invariants OK. ({grandfathered} grandfathered by baseline)"
            )

    return 1 if any(v.rule != "editor-no-hardcode-pending" for v in new_violations) else 0


if __name__ == "__main__":
    sys.exit(main())
