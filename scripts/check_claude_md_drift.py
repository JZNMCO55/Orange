#!/usr/bin/env python3
# 检测 CLAUDE.md 中的"项目状态"段是否与实际 design-plan.md / editor-roadmap.md
# 漂移。开发推进时 CLAUDE.md 的 Phase status 段 / "Until Phase 1 / Task NN lands"
# 类时态描述会迅速过时，每个新 session 读到的是过期快照——这个脚本把漂移定量化。
#
# 退出码：0 = 无漂移；1 = 检测到漂移。
#
# 用法：python scripts/check_claude_md_drift.py [--verbose]

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# Windows PowerShell 默认 cp936，会卡 ✅ / 中文输出。强制 UTF-8。
try:
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
except Exception:
    pass

REPO_ROOT = Path(__file__).resolve().parent.parent

CLAUDE_MD = REPO_ROOT / "CLAUDE.md"
DESIGN_PLAN = REPO_ROOT / "docs" / "design-plan.md"
EDITOR_ROADMAP = REPO_ROOT / "docs" / "editor-roadmap.md"

# 触发漂移的字面量：这些字符串描述的是 pre-Phase-1 / 早期状态，
# 当任何 Phase 1 task 已 ✅ 时它们就是错的。
STALE_LITERALS = [
    "pre-Phase-1",
    "implementation has not started",
    "Phase 1: not started",
    "Phases 2–5.5 outlines only",
    "Phases 2-5.5 outlines only",
    "Until Phase 1 / Task 02 lands",
    "script does not exist yet",
    "legacy `Src/` skeleton",
    "legacy Src/ skeleton",
    "scheduled for removal in Phase 1 / Task 01",
]


def count_done_tasks(path: Path, phase_prefix: str) -> tuple[int, int]:
    """统计 `### Phase N` ~ 下一 `### Phase` 之间的 #### Task 数 + ✅ 数。"""
    if not path.exists():
        return (0, 0)
    text = path.read_text(encoding="utf-8")
    # 切片到指定 Phase 的 task 段
    phase_re = re.compile(rf"^### Phase {phase_prefix}\b.*$", re.MULTILINE)
    next_phase_re = re.compile(r"^### Phase\b", re.MULTILINE)
    m = phase_re.search(text)
    if not m:
        return (0, 0)
    start = m.end()
    nxt = next_phase_re.search(text, m.end())
    end = nxt.start() if nxt else len(text)
    body = text[start:end]
    task_lines = re.findall(r"^####\s+Task\s+\d+[a-z]?[:：].*$", body, re.MULTILINE)
    done = sum(1 for line in task_lines if "✅" in line)
    return (done, len(task_lines))


def detect_drift(verbose: bool = False) -> list[str]:
    issues: list[str] = []
    if not CLAUDE_MD.exists():
        return ["CLAUDE.md 不存在"]
    claude_text = CLAUDE_MD.read_text(encoding="utf-8")

    # 1) 字面量过期检测
    for literal in STALE_LITERALS:
        if literal in claude_text:
            # 找出现行号供报告
            for i, line in enumerate(claude_text.splitlines(), start=1):
                if literal in line:
                    issues.append(
                        f"CLAUDE.md:{i}: 包含已过期字面量 `{literal}` —— 与 design-plan.md 实际进度冲突"
                    )

    # 2) 量化 Phase 完成度，与 CLAUDE.md 状态段比对
    phase_status: dict[str, tuple[int, int]] = {}
    for phase in ["1", "2", "3", "4", "5", "5.5"]:
        phase_status[phase] = count_done_tasks(DESIGN_PLAN, phase)

    if verbose:
        print("design-plan.md Phase 实际完成度：")
        for phase, (done, total) in phase_status.items():
            print(f"  Phase {phase}: {done}/{total} tasks ✅")

    # 3) 关键事实检测：若任何 Phase 1 task ✅，则 CLAUDE.md 中
    # "pre-Phase-1" / "implementation has not started" 必然是错的（已被字面量规则覆盖）
    p1_done, p1_total = phase_status.get("1", (0, 0))
    if p1_done > 0 and "pre-Phase-1" in claude_text:
        issues.append(
            f"语义漂移：Phase 1 已完成 {p1_done}/{p1_total} task ✅，但 CLAUDE.md 仍声称 pre-Phase-1"
        )

    # 4) 顶层 CMakeLists.txt 实际状态
    top_cmake = REPO_ROOT / "CMakeLists.txt"
    if top_cmake.exists() and "no top-level `CMakeLists.txt`" in claude_text:
        issues.append(
            "语义漂移：顶层 CMakeLists.txt 已存在，但 CLAUDE.md 仍声称尚未建立"
        )

    # 5) fetch_and_build_3rdparty.py 实际状态
    fetch_script = REPO_ROOT / "scripts" / "fetch_and_build_3rdparty.py"
    if fetch_script.exists() and "script does not exist yet" in claude_text:
        issues.append(
            "语义漂移：scripts/fetch_and_build_3rdparty.py 已存在，但 CLAUDE.md 仍声称未建"
        )

    # 6) Legacy Src/ 检测（大小写敏感）
    legacy_src = REPO_ROOT / "Src"
    if not legacy_src.exists() and "legacy `Src/` skeleton" in claude_text:
        issues.append(
            "语义漂移：legacy Src/ 已被清理，但 CLAUDE.md 仍描述其待清理"
        )

    # 7) Editor 状态：检查 editor-roadmap.md 中已 ✅ 的 milestone
    if EDITOR_ROADMAP.exists():
        roadmap = EDITOR_ROADMAP.read_text(encoding="utf-8")
        # 扫描 ### v0.x · ... ✅
        done_milestones = re.findall(r"^###\s+(v[\d.]+)[^✅\n]*✅", roadmap, re.MULTILINE)
        if verbose:
            print(f"editor-roadmap.md 已完成 milestone: {done_milestones}")

    return issues


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="CLAUDE.md 漂移检测")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args(argv)

    issues = detect_drift(verbose=args.verbose)
    if not issues:
        print("CLAUDE.md drift: none detected.")
        return 0
    for issue in issues:
        print(issue)
    print(f"\n{len(issues)} drift issue(s).", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
