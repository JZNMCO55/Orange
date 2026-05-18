# assets/environments/

存放场景的 HDR 环境贴图（IBL 源数据）。`EnvironmentComponent.cubemap`
字段引用本目录下的 `.hdr` 文件，Pipeline 启动期由 `IblBaker` 重采样到
6-face cubemap、烘焙 irradiance + prefiltered specular 后喂给 PBR shader。

## 默认 IBL 资产（`default_outdoor.hdr`）

**当前阶段未自动入库**：5 MB 量级的二进制 HDR 文件不进 git 仓主线，避免
hot path commit 历史膨胀；由 sample 启动前 fetch / 由测试人员一次性下载。

### 下载步骤

1. 打开 https://polyhaven.com/hdris （Poly Haven，CC0 公有领域许可证零摩擦）
2. 任选一张 outdoor 类（如 `golden_gate_hills` / `kloofendal_43d_clear`
   / `noon_grass` 等典型 hemisphere sun-lit scene）
3. 在 download 面板选择 **1K** 分辨率 + **HDR (.hdr)** 格式（约 5 MB）
4. 把下载到的文件**重命名**为 `default_outdoor.hdr`，放进 `assets/environments/`
   目录下

### 验证

启动 `samples/14_pbr_ibl`（c8 落地后可用）应自动加载本目录下的
`default_outdoor.hdr`；金属球能反映出环境内容、阴影区有环境光填充。
未放置 HDR 文件时 sample 退化到 dummy IBL（场景仍可渲染，但金属球反射
为黑、阴影区无环境填充）。

## 许可证

Poly Haven HDRI 全部 **CC0 1.0 Universal** 许可证（公有领域），可以自由
商用 / 修改 / 再分发，无署名要求。

## 后续

- 多张 HDR 共存（室内 / 室外 / 黄昏）+ 编辑器侧 Environment 浏览器 ——
  推到独立 v0.8 编辑器伴随 milestone（见 `docs/pbr-ibl-milestone.md`
  §Out-of-scope）
- 离线 cook：把 `.hdr` 预烘焙成 ORTX 容器（`R32G32B32A32_Float` format）
  入仓 —— 推到 Phase 9 资产管线 milestone
