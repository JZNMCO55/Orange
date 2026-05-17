"""OrangeEditor brand 资产构建脚本。

输入：master logo PNG（orange-editor-logo.png，1254x1254 RGB 深炭灰背景版本）
输出：
  * orange-editor-logo.ico ——  Windows 多分辨率 icon（16/24/32/48/64/128/256），
    供同目录 OrangeEditor.rc 通过 `1 ICON "..."` 嵌入 exe，让 Windows 文件资
    源管理器 / Alt-Tab / taskbar 在不同 DPI / 上下文挑合适尺寸
  * EditorWindowIconData.h —— 把 16 / 32 / 48 三档 RGBA 像素以 constexpr
    uint8_t 数组形式 codegen 进头文件，由 EditorWindowIcon.cpp 包，喂给
    glfwSetWindowIcon。选择"内嵌 constexpr 数组"而非"运行期读 PNG 文件"
    是为了避免再 vendor 一份 stb_image.h（仓内 vendor/stb/ 当前只放
    stb_image_write.h，3rdparty.json 已说明 stb_image 不入编辑器依赖），
    且不依赖运行期 cwd（虽然 main.cpp 有 ChdirToRepoRoot，但 brand 资产
    嵌进 exe 更稳）

幂等：检测输入 mtime 与输出 mtime 比较；输出全部 up-to-date 时跳过。

用法：
    python tools/OrangeEditor/branding/build_icon.py
    python tools/OrangeEditor/branding/build_icon.py --force

依赖：Pillow（pip install pillow）。Pillow 12.x 内置 ICO encoder 支持多
分辨率打包；不需要 ImageMagick。

设计注意：当前 master PNG 是 8-bit RGB（无 alpha 通道）—— BRANDING.md
设计 prompt 指定的"深炭灰 #2A2A2A 背景版本"。本脚本不做背景 chroma-key
（用户验收要求"不做颜色魔法"），全部生成"深炭灰背景 + opaque alpha"的
ICO / RGBA blob。透明背景版本属未来 brand session 范畴（需手工抠图 /
重新生成 logo），与本脚本正交。
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("[build_icon] 缺少 Pillow，请 `pip install pillow`", file=sys.stderr)
    sys.exit(1)


# ICO 内多分辨率档位。Windows 选择策略：文件资源管理器小图标 16/32，
# 大图标 / Alt-Tab 48，jump list 64，windows 11 任务栏高 DPI 128/256。
# 全部覆盖一次到位，体积也不大（< 200 KB）。
ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)

# GLFWimage 内嵌档位。GLFW 在 glfwSetWindowIcon 内自己挑最接近系统目标的
# 那张做 nearest-neighbor / bilinear 缩放；docs 推荐 16/32/48 三档覆盖。
# 更大的 64+ 给 ICO 跑就行，GLFW 路径不重复内嵌避免 .exe 膨胀。
GLFW_SIZES = (16, 32, 48)


def newer_than(src: Path, *outputs: Path) -> bool:
    """src 的 mtime 比任一 output 都新（含 output 不存在）—— 需要重建。"""
    if not src.exists():
        raise FileNotFoundError(src)
    src_mtime = src.stat().st_mtime
    for out in outputs:
        if not out.exists() or out.stat().st_mtime < src_mtime:
            return True
    return False


def build_ico(master: Image.Image, out_path: Path) -> None:
    """PIL 内置 ICO encoder：把同一张 master 按 ICO_SIZES 多分辨率打包。

    PIL 的 save(format='ICO') 默认会按 sizes 参数把图自身缩放成各档；为了
    避免 PIL 内部用低质量算法（默认 NEAREST 在小尺寸出锯齿），手工先用
    LANCZOS 缩放成最小档（16），再让 PIL save 时把整张 master 按 sizes 收
    一次—— PIL 12.x 已默认 LANCZOS，但显式传 sizes 仍是更可控的路径。
    """
    # ICO container expects RGBA; master 是 RGB，先升级。
    rgba = master.convert("RGBA")
    rgba.save(
        out_path,
        format="ICO",
        sizes=[(s, s) for s in ICO_SIZES],
    )
    print(f"[build_icon] wrote {out_path}  ({len(ICO_SIZES)} sizes: "
          f"{', '.join(str(s) for s in ICO_SIZES)})")


def build_glfw_header(master: Image.Image, out_path: Path) -> None:
    """把 16/32/48 RGBA 像素 codegen 成 constexpr uint8_t 数组头文件。

    布局：每档一个 kEditorWindowIcon<NxN>[N*N*4]，外加索引数组
    kEditorWindowIcons[]，让 .cpp 一次性 push 进 GLFWimage[] 容器。

    选 constexpr 数组而非 #embed —— C++20 没标准 #embed（C23 才有），MSVC
    extension 也未覆盖；codegen 文本数组是 C++20 唯一稳路径。
    """
    lines: list[str] = []
    lines.append("// 本文件由 tools/OrangeEditor/branding/build_icon.py 生成。")
    lines.append("// 不要手动编辑——重跑脚本即可重新生成。")
    lines.append("//")
    lines.append("// 内容：OrangeEditor logo 多分辨率 RGBA 像素，喂给 glfwSetWindowIcon。")
    lines.append("// master：tools/OrangeEditor/branding/orange-editor-logo.png")
    lines.append("")
    lines.append("#ifndef ORANGE_EDITOR_BRANDING_EDITOR_WINDOW_ICON_DATA_H")
    lines.append("#define ORANGE_EDITOR_BRANDING_EDITOR_WINDOW_ICON_DATA_H")
    lines.append("")
    lines.append("#include <cstdint>")
    lines.append("#include <cstddef>")
    lines.append("")
    lines.append("namespace OrangeEditorBranding")
    lines.append("{")
    lines.append("")
    lines.append("struct EditorWindowIconEntry")
    lines.append("{")
    lines.append("    int                  width;")
    lines.append("    int                  height;")
    lines.append("    const std::uint8_t*  pixels;  // RGBA8，逐行排列，宽优先")
    lines.append("};")
    lines.append("")

    rgba = master.convert("RGBA")

    for size in GLFW_SIZES:
        scaled = rgba.resize((size, size), Image.LANCZOS)
        raw = scaled.tobytes("raw", "RGBA")
        var = f"kEditorWindowIcon{size}x{size}"
        lines.append(f"inline constexpr std::uint8_t {var}[{len(raw)}] = {{")
        # 每行 16 字节，hex 格式
        per_row = 16
        for i in range(0, len(raw), per_row):
            chunk = raw[i:i + per_row]
            hex_chunk = ", ".join(f"0x{b:02X}" for b in chunk)
            terminator = "," if (i + per_row) < len(raw) else ""
            lines.append(f"    {hex_chunk}{terminator}")
        lines.append("};")
        lines.append("")

    # 索引数组
    lines.append("inline constexpr EditorWindowIconEntry kEditorWindowIcons[] = {")
    for size in GLFW_SIZES:
        var = f"kEditorWindowIcon{size}x{size}"
        lines.append(f"    {{ {size}, {size}, {var} }},")
    lines.append("};")
    lines.append("")
    lines.append("inline constexpr std::size_t kEditorWindowIconCount =")
    lines.append("    sizeof(kEditorWindowIcons) / sizeof(kEditorWindowIcons[0]);")
    lines.append("")
    lines.append("}  // namespace OrangeEditorBranding")
    lines.append("")
    lines.append("#endif  // ORANGE_EDITOR_BRANDING_EDITOR_WINDOW_ICON_DATA_H")
    lines.append("")

    out_path.write_text("\n".join(lines), encoding="utf-8")
    total_bytes = sum(size * size * 4 for size in GLFW_SIZES)
    print(f"[build_icon] wrote {out_path}  ({len(GLFW_SIZES)} sizes: "
          f"{', '.join(str(s) for s in GLFW_SIZES)}; ~{total_bytes} B RGBA)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--force",
        action="store_true",
        help="忽略 mtime 检查，强制重新生成",
    )
    args = parser.parse_args()

    here = Path(__file__).resolve().parent
    master_path = here / "orange-editor-logo.png"
    ico_path = here / "orange-editor-logo.ico"
    header_path = here / "EditorWindowIconData.h"

    if not master_path.exists():
        print(f"[build_icon] master 不存在: {master_path}", file=sys.stderr)
        return 1

    if not args.force and not newer_than(master_path, ico_path, header_path):
        print("[build_icon] 产物均比 master 新，跳过（用 --force 强制重建）")
        return 0

    print(f"[build_icon] loading {master_path}")
    with Image.open(master_path) as master:
        master.load()
        print(f"[build_icon] master: {master.size[0]}x{master.size[1]} {master.mode}")
        build_ico(master, ico_path)
        build_glfw_header(master, header_path)

    return 0


if __name__ == "__main__":
    sys.exit(main())
