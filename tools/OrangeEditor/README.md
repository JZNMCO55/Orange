# OrangeEditor

OrangeEngine 的关卡 / 粒子 / 材质编辑器，目标是把 game content 生产从
"程序员手写 JSON" 过渡到"美术 / 关卡设计师交互式编辑"。

**当前状态（Phase 6 / Task 06-01）**：v0.0 scaffold —— 一个能开窗 + Esc
退出的最小可执行体，证明 `find_package(OrangeEngine)` 消费链通；尚未
接 ImGui / 实体树 / 检视器，那些是后续 Task 06-02 ~ 06-07 增量建。

## 架构约束

编辑器**刻意不在引擎主仓 root 的 `add_subdirectory` 链上**：本目录是
顶级独立 CMake 工程，仅通过 `find_package(OrangeEngine CONFIG REQUIRED)`
消费引擎。这是 Phase 6 / Task 06-01 的核心约束 ——

> "编辑器通过 find_package(OrangeEngine) 依赖引擎，不直接吃源；这是
>  引擎 API 自身可消费性的最强验证"

等同于游戏仓库消费引擎的方式：先把引擎安装到一个 prefix，再用
`-DCMAKE_PREFIX_PATH=<install-prefix>` 配置编辑器。

## Build 流程

两阶段构建：先安装引擎，再 configure + build 编辑器。

### Step 1 — 安装引擎到 scratch prefix

```powershell
# 在引擎仓 root 下
cmake -S . -B build -DCMAKE_PREFIX_PATH="D:/3rdparty/install"
cmake --build build --config Debug
cmake --install build --config Debug --prefix build/install_tree
```

`build/install_tree` 是个 throwaway prefix，每次重新 install 即可；
正式发布时按 GNUInstallDirs 装到系统 prefix。

### Step 2 — Configure + build OrangeEditor

```powershell
# 在引擎仓 root 下
cmake -S tools/OrangeEditor `
      -B tools/OrangeEditor/build `
      "-DCMAKE_PREFIX_PATH=$PWD/build/install_tree;D:/3rdparty/install"
cmake --build tools/OrangeEditor/build --config Debug
```

`CMAKE_PREFIX_PATH` 同时指向：
- `build/install_tree` —— 上一步装的 OrangeEngine SDK
- `D:/3rdparty/install` —— OrangeRender SDK + glm / glfw / EnTT / nlohmann_json 等
  传递依赖（OrangeEngineConfig.cmake 内的 `find_dependency` 会找到这条链）

### Step 3 — 运行

```powershell
tools/OrangeEditor/build/Debug/OrangeEditor.exe
```

按 Esc 或关窗按钮退出。

## CI 自动验证

`tests/install/editor_build_smoke.cmake` 会自动跑这套两阶段流程：

```powershell
ctest --test-dir build -C Debug -R editor_build_smoke
```

该 ctest case 把引擎装到一个 throwaway prefix，再 configure + build
本编辑器，证明 `find_package` 消费链不回归。仅 build 不 run（CI 一般
没有 GPU + display）。

## 后续路线

`docs/roadmap.md` 中 Phase 6 的剩余 Task：

- Task 06-02 · ImGui 集成与 dock space
- Task 06-03 · 实体树视图（World registry 可视化）
- Task 06-04 · 组件检视器（内置组件可编辑控件）
- Task 06-05 · 粒子编辑器子模式
- Task 06-06 · 材质编辑器子模式
- Task 06-07 · 场景保存 / 加载流程（接 `Scene::Serialize`）

各 Task 之间相对独立，按需推进。Task 06-04 真要画 viewport 时会需要
解决"内置 SPV shader 部署到 editor exe 旁边"的问题；当前 Task 06-01
范围不含。
