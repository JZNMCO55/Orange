# OrangeEditor Branding 资产

## 文件

- `orange-editor-logo.png` —— 1024×1024 master logo（深炭灰 / 黑背景，8-bit RGBA 带 alpha）
- `orange-editor-logo.ico` —— Windows 多分辨率 icon（16/24/32/48/64/128/256），由 `build_icon.py` 从 master PNG 生成
- `EditorWindowIconData.h` —— 16/32/48 三档 RGBA constexpr 数组，由 `build_icon.py` 从 master PNG 生成，喂给 `glfwSetWindowIcon`
- `OrangeEditor.rc` —— Windows resource 脚本，把 `.ico` 嵌进 exe（数字 ID 1 = Explorer 约定的 app icon）
- `EditorWindowIcon.{h,cpp}` —— `ApplyEditorWindowIcons(GLFWwindow*)` 把内嵌 RGBA 数组转 `GLFWimage[]` 喂 GLFW
- `build_icon.py` —— brand 资产构建脚本（依赖 Pillow）

## 来源

- 生成工具：ChatGPT / DALL-E 3
- 生成日期：2026-05-17（v0.6.5 milestone ✅ 后续 brand 工作；当晚迭代到 v2 行星 + 光环版本）
- 设计决策由来：见 `docs/decisions/ADR-002-editor-visual-system.md` §视觉体系
- 设计方向（v2 当前）：橘色行星 + 发光内核（中心白热点） + 倾斜光环（neon 黄白带轨迹粒子）+ 绿色叶柄（保留 fruit 身份）+ 黑色背景 + alpha 镂空

## 设计 prompt（摘要，完整迭代过程见 v0.6.5 session）

```
A modern flat vector software logo for "OrangeEditor", a 2D/2.5D game engine
editor. Stylized orange fruit with simplified flat green stem and one tiny leaf.
Smooth gradient orange #FF8A3D body (NO realistic surface texture, NO pores).
Bright white core glowing from center, single soft diffused halo radiating
outward (NOT concentric bullseye rings). Thin neon-orange outline rim along the
fruit's contour. Subtle volumetric body shading (not flat-flat, not 3D
photoreal). Dark charcoal #2A2A2A background. App icon aesthetic — think Figma
or Substance Painter logo style, NOT a fruit illustration. Must read clearly at
32×32 pixels.
```

## 接入状态（2026-05-17 完工）

1. **PNG → ICO 多分辨率打包** ✅
   - 由 `build_icon.py` 用 Pillow 生成 `orange-editor-logo.ico`（16/24/32/48/64/128/256 七档）
   - 选 Pillow 而非 ImageMagick：项目已有 Python toolchain，无须额外二进制依赖
   - 透明度：未做——master PNG 是 8-bit RGB 深炭灰背景，整张图升 RGBA 后 alpha=255。透明背景版本属未来 brand 工作（需重新生成 logo 或手工抠图），与本期正交

2. **Windows resource (.rc) 文件接入 exe** ✅
   - `OrangeEditor.rc` 用裸数字 ID 1（Explorer 约定的 app icon），不依赖 `resource.h` 符号
   - CMake 把 `.rc` 加进 `add_executable(OrangeEditor ...)` source list；MSVC 自动调 rc.exe 编 .res 并 link 进 exe
   - 验证：`[System.Drawing.Icon]::ExtractAssociatedIcon` 从 exe 取出 32×32 icon = 橘子 logo

3. **GLFW 窗口 icon** ✅
   - `ApplyEditorWindowIcons(GLFWwindow*)` 在 main.cpp 创建 AppHost 后立即调用
   - 数据走内嵌 `EditorWindowIconData.h`（16/32/48 三档 constexpr RGBA），不依赖运行期文件 IO / cwd
   - 选内嵌 constexpr 而非 stb_image 解 PNG：避免再 vendor 一份 stb_image.h（仓内 `vendor/stb/` 当前只放 stb_image_write.h，3rdparty.json 已说明 stb_image 不入引擎依赖）
   - 启动日志验证：`[OrangeEditor] applied window icon (3 sizes)`
   - 多视口 caveat：GLFW 不会让 multi-viewport 子窗口自动继承主窗口 icon。当前仅主窗口 set；用户把 panel 拖出成独立 native window 时子窗口仍用 OS 默认 icon。修法是在 ImGui Platform_CreateWindow 回调后对每个 sub-viewport 的 GLFWwindow 再调一次 `ApplyEditorWindowIcons`——属后续 polish，本期不做
   - 小尺寸 caveat：v2 设计在 16×16 / 24×24 档橘色光环细节会被 LANCZOS 缩成噪点（光环线宽 < 1 像素），近距离看是橘色团块。视觉上仍能识别 brand，taskbar / Alt-Tab 距离够远不显眼；不另出"小尺寸专版" simplified glyph

## 重建 brand 资产

修改 `orange-editor-logo.png` 后跑：

```
python tools/OrangeEditor/branding/build_icon.py
```

脚本幂等：默认按 mtime 跳过，加 `--force` 强制重建。产物 `.ico` 和 `.h` 入库（与 master PNG 同等地位），不在 build 时动态生成——保证 CMake 不依赖 Python 路径。

## 许可证

- Logo PNG 由项目所有者（OrangeEngine project owner）通过 OpenAI DALL-E 3 生成；按 OpenAI 服务条款，生成图像版权归用户所有
- 后续如需公开发布 / 分发使用，请确认 OpenAI 当时服务条款是否要求 attribution
