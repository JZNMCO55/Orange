# OrangeEditor Branding 资产

## 文件

- `orange-editor-logo.png` —— 1024×1024 master logo（深炭灰 #2A2A2A 背景版本）

## 来源

- 生成工具：ChatGPT / DALL-E 3
- 生成日期：2026-05-17（v0.6.5 milestone ✅ 后续 brand 工作）
- 设计决策由来：见 `docs/decisions/ADR-002-editor-visual-system.md` §视觉体系
- 设计方向：橘子（brand 形象）+ 发光内核（A 多层 glow）+ neon 外轮廓（B outline glow）+ flat vector 风格（不是 photorealistic fruit）

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

## 后续 TODO（独立 session 处理）

logo 当前**仅作为静态资产入库**，未接入 OrangeEditor 任何运行时 / 构建时路径。下个 brand session 需要：

1. **PNG → ICO 多分辨率打包**（16/24/32/48/64/128/256）
   - Windows `.ico` 文件让 OS 在不同 DPI / 上下文（taskbar / 文件管理器 / Alt-Tab）选合适尺寸
   - 工具候选：PowerShell `System.Drawing.Icon` / Python Pillow / ImageMagick
   - 透明度处理：当前 PNG 是深炭灰背景；ICO 通常要透明背景版本（让 OS 自己处理背景），需要从 PNG 抠图（remove.bg 或 Photoshop）

2. **Windows resource (.rc) 文件接入 exe**
   - 新建 `tools/OrangeEditor/branding/OrangeEditor.rc` 含 `IDI_ICON1 ICON "orange-editor-logo.ico"`
   - `tools/OrangeEditor/CMakeLists.txt` 把 `.rc` 加进 `add_executable(OrangeEditor ...)` source list
   - MSVC 自动 link 资源进 exe，文件资源管理器立即显示 logo

3. **GLFW 窗口 icon**（运行期窗口左上角 / 任务栏 active icon）
   - `tools/OrangeEditor/main.cpp` 在 AppHost 创建后加 `glfwSetWindowIcon(window, count, GLFWimage[])`
   - 加载 logo PNG 或多分辨率版本喂给 GLFW
   - 注意：GLFWimage 是 RGBA 8-bit，stb_image 加载 PNG 即可消费

## 许可证

- Logo PNG 由项目所有者（OrangeEngine project owner）通过 OpenAI DALL-E 3 生成；按 OpenAI 服务条款，生成图像版权归用户所有
- 后续如需公开发布 / 分发使用，请确认 OpenAI 当时服务条款是否要求 attribution
